#include <assert.h>
#include <bare.h>
#include <js.h>
#include <shellapi.h>
#include <utf.h>

#include "windows-app-sdk.h"

struct bare_win_ui_web_view_t {
  WebView2 handle;

  js_env_t *env;
  js_ref_t *ctx;
  js_ref_t *on_ready;
  js_ref_t *on_message;

  event_token message_token;
  bool message_handler = false;
  bool destroyed = false;
  bool references_deleted = false;
  bool finalized = false;
  unsigned int pending_operations = 1;
};

static void
bare_win_ui_web_view__destroy(bare_win_ui_web_view_t *self) {
  if (self->destroyed) return;

  self->destroyed = true;

  auto core = self->handle.CoreWebView2();
  if (core && self->message_handler) {
    core.WebMessageReceived(self->message_token);
    self->message_handler = false;
  }
}

static void
bare_win_ui_web_view__delete_references(bare_win_ui_web_view_t *self) {
  int err;

  if (self->references_deleted) return;

  self->references_deleted = true;

  err = js_delete_reference(self->env, self->on_message);
  assert(err == 0);

  err = js_delete_reference(self->env, self->on_ready);
  assert(err == 0);

  err = js_delete_reference(self->env, self->ctx);
  assert(err == 0);
}

static void
bare_win_ui_web_view__maybe_release(bare_win_ui_web_view_t *self) {
  if (self->finalized && self->pending_operations == 0) delete self;
}

static void
bare_win_ui_web_view__on_release(js_env_t *env, void *data, void *finalize_hint) {
  auto self = reinterpret_cast<bare_win_ui_web_view_t *>(data);

  self->finalized = true;
  bare_win_ui_web_view__destroy(self);
  bare_win_ui_web_view__delete_references(self);
  bare_win_ui_web_view__maybe_release(self);
}

static void
bare_win_ui_web_view__on_message(bare_win_ui_web_view_t *self, hstring const &message) {
  int err;

  if (self->destroyed) return;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(self->env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(self->env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_message;
  err = js_get_reference_value(self->env, self->on_message, &on_message);
  assert(err == 0);

  js_value_t *args[1];
  err = js_create_string_utf16le(
    self->env,
    reinterpret_cast<const utf16_t *>(message.data()),
    message.size(),
    &args[0]
  );
  assert(err == 0);

  err = js_call_function(self->env, ctx, on_message, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(self->env, scope);
  assert(err == 0);
}

static void
bare_win_ui_web_view__on_ready(bare_win_ui_web_view_t *self, AsyncStatus const &status) {
  int err;

  if (self->destroyed) return;

  auto env = self->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_ready;
  err = js_get_reference_value(env, self->on_ready, &on_ready);
  assert(err == 0);

  js_value_t *args[1];

  if (status == AsyncStatus::Completed) {
    auto core = self->handle.CoreWebView2();

    self->message_token = core.WebMessageReceived([=](auto &, auto &args) {
      auto message = args.TryGetWebMessageAsString();
      bare_win_ui_web_view__on_message(self, message);
    });
    self->message_handler = true;

    err = js_get_null(env, &args[0]);
    assert(err == 0);
  } else {
    wchar_t const error[] = L"WebView2 initialization failed";

    err = js_create_string_utf16le(
      env,
      reinterpret_cast<const utf16_t *>(error),
      sizeof(error) / sizeof(error[0]) - 1,
      &args[0]
    );
    assert(err == 0);
  }

  err = js_call_function(env, ctx, on_ready, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static js_value_t *
bare_win_ui_web_view_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 3;
  js_value_t *argv[3];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 3);

  auto web_view = new bare_win_ui_web_view_t();

  web_view->env = env;

  err = js_create_reference(env, argv[0], 1, &web_view->ctx);
  assert(err == 0);

  err = js_create_reference(env, argv[1], 1, &web_view->on_ready);
  assert(err == 0);

  err = js_create_reference(env, argv[2], 1, &web_view->on_message);
  assert(err == 0);

  js_value_t *result;
  err = js_create_external(env, web_view, bare_win_ui_web_view__on_release, nullptr, &result);
  assert(err == 0);

  auto req = web_view->handle.EnsureCoreWebView2Async();

  DispatcherQueue dispatcher = DispatcherQueue::GetForCurrentThread();

  req.Completed([=](auto &, auto &status) {
    auto completion = status;

    dispatcher.TryEnqueue([=] {
      bare_win_ui_web_view__on_ready(web_view, completion);
      web_view->pending_operations--;
      bare_win_ui_web_view__maybe_release(web_view);
    });
  });

  return result;
}

static js_value_t *
bare_win_ui_web_view_width(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1 || argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  js_value_t *result = nullptr;

  if (argc == 1) {
    err = js_create_double(env, web_view->handle.Width(), &result);
    assert(err == 0);
  } else {
    double width;
    err = js_get_value_double(env, argv[1], &width);
    assert(err == 0);

    web_view->handle.Width(width);
  }

  return result;
}

static js_value_t *
bare_win_ui_web_view_height(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1 || argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  js_value_t *result = nullptr;

  if (argc == 1) {
    err = js_create_double(env, web_view->handle.Height(), &result);
    assert(err == 0);
  } else {
    double height;
    err = js_get_value_double(env, argv[1], &height);
    assert(err == 0);

    web_view->handle.Height(height);
  }

  return result;
}

static js_value_t *
bare_win_ui_web_view_source(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1 || argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  js_value_t *result = nullptr;

  if (argc == 1) {
    auto source = web_view->handle.Source().AbsoluteUri();

    err = js_create_string_utf16le(env, reinterpret_cast<const utf16_t *>(source.data()), source.size(), &result);
    assert(err == 0);
  } else {
    size_t len;
    err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
    assert(err == 0);

    std::vector<wchar_t> uri(len);
    err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(uri.data()), len, nullptr);
    assert(err == 0);

    web_view->handle.Source(Uri(hstring(uri.data(), len)));
  }

  return result;
}

static js_value_t *
bare_win_ui_web_view_navigate(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> uri(len);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(uri.data()), len, nullptr);
  assert(err == 0);

  auto core = web_view->handle.CoreWebView2();
  assert(core);
  assert(!web_view->destroyed);

  core.Navigate(hstring(uri.data(), len));

  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_navigate_to_string(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> html(len);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(html.data()), len, nullptr);
  assert(err == 0);

  auto core = web_view->handle.CoreWebView2();
  assert(core);
  assert(!web_view->destroyed);

  core.NavigateToString(hstring(html.data(), len));

  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_post_message(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> message(len);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(message.data()), len, nullptr);
  assert(err == 0);

  auto core = web_view->handle.CoreWebView2();
  assert(core);
  assert(!web_view->destroyed);

  core.PostWebMessageAsString(hstring(message.data(), len));

  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_open_external(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 2);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> url(len + 1);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(url.data()), len, nullptr);
  assert(err == 0);
  url[len] = L'\0';

  try {
    Uri uri(hstring(url.data(), len));
    auto scheme = uri.SchemeName();
    if (scheme != L"http" && scheme != L"https") {
      js_throw_error(env, "ERR_INVALID_ARGUMENT", "external URL must use http or https");
      return nullptr;
    }
  } catch (hresult_error const &) {
    js_throw_error(env, "ERR_INVALID_ARGUMENT", "external URL is invalid");
    return nullptr;
  }

  auto result = ShellExecuteW(nullptr, L"open", url.data(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    js_throw_error(env, "ERR_EXTERNAL_OPEN", "could not open external URL");
  }

  (void) web_view;
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_open_dev_tools_window(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  auto core = web_view->handle.CoreWebView2();
  assert(core);
  assert(!web_view->destroyed);

  core.OpenDevToolsWindow();

  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_destroy(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  bare_win_ui_web_view__destroy(web_view);
  bare_win_ui_web_view__delete_references(web_view);

  return nullptr;
}
