#include <assert.h>
#include <bare.h>
#include <js.h>
#include <shellapi.h>
#include <utf.h>

#include <atomic>
#include <cwchar>
#include <memory>
#include <mutex>

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

  std::weak_ptr<bare_win_ui_web_view_t> owner;
  DispatcherQueue dispatcher = nullptr;
  std::mutex ready_lock;
  AsyncStatus pending_ready_status;
  bool ready_pending = false;
  std::mutex script_lock;
  AsyncStatus pending_script_status;
  bool script_pending = false;
};

static std::atomic<bool> bare_win_ui_web_view__test_hold_ready = false;
static std::atomic<bool> bare_win_ui_web_view__test_hold_script = false;
static std::atomic<bool> bare_win_ui_web_view__test_fail_script = false;

static constexpr wchar_t bare_win_ui_web_view__bridge_script[] = LR"BARE(
(function () {
  const bridge = Object.freeze({
    postMessage(message) {
      if (typeof message === 'string') {
        window.chrome.webview.postMessage(message)
      }
    }
  })

  Object.defineProperty(window, 'bareNative', {
    configurable: false,
    value: bridge
  })

  window.chrome.webview.addEventListener('message', (event) => {
    if (typeof event.data !== 'string') return
    window.dispatchEvent(new MessageEvent('bare-native-message', { data: event.data }))
  })
})()
)BARE";

static void
bare_win_ui_web_view__destroy(bare_win_ui_web_view_t *self) {
  if (self->destroyed) return;

  self->destroyed = true;

  if (self->message_handler) {
    auto core = self->handle.CoreWebView2();
    if (core) core.WebMessageReceived(self->message_token);
    self->message_handler = false;
  }

  self->handle = nullptr;
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
bare_win_ui_web_view__on_release(js_env_t *env, void *data, void *finalize_hint) {
  auto owner = reinterpret_cast<std::shared_ptr<bare_win_ui_web_view_t> *>(finalize_hint);
  auto self = owner != nullptr ? owner->get() : reinterpret_cast<bare_win_ui_web_view_t *>(data);

  if (self != nullptr) {
    self->finalized = true;
    bare_win_ui_web_view__destroy(self);
    bare_win_ui_web_view__delete_references(self);
  }

  if (owner != nullptr) {
    owner->reset();
    delete owner;
  }
}

static void
bare_win_ui_web_view__on_message(bare_win_ui_web_view_t *self, hstring const &message) {
  int err;

  if (self->finalized || self->destroyed) return;

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
bare_win_ui_web_view__on_ready(
  bare_win_ui_web_view_t *self,
  AsyncStatus const &status,
  wchar_t const *error_message = L"WebView2 initialization failed"
) {
  int err;

  if (self->finalized || self->destroyed) return;

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

    auto owner = self->owner;
    self->message_token = core.WebMessageReceived([owner](auto &, auto &args) {
      try {
        auto message = args.TryGetWebMessageAsString();
        if (auto self = owner.lock()) {
          bare_win_ui_web_view__on_message(self.get(), message);
        }
      } catch (hresult_error const &) {
        // WebView2 throws when a page sends a non-string message. The
        // portable Bare WebView contract only forwards strings.
      }
    });
    self->message_handler = true;

    err = js_get_null(env, &args[0]);
    assert(err == 0);
  } else {
    err = js_create_string_utf16le(
      env,
      reinterpret_cast<const utf16_t *>(error_message),
      wcslen(error_message),
      &args[0]
    );
    assert(err == 0);
  }

  err = js_call_function(env, ctx, on_ready, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static void
bare_win_ui_web_view__on_script_ready(
  std::shared_ptr<bare_win_ui_web_view_t> const &web_view,
  AsyncStatus status,
  DispatcherQueue const &dispatcher
) {
  auto self = web_view.get();
  if (self->finalized || self->destroyed) return;

  if (bare_win_ui_web_view__test_fail_script.exchange(false)) {
    status = AsyncStatus::Error;
  }

  {
    std::lock_guard guard(self->script_lock);
    if (bare_win_ui_web_view__test_hold_script.exchange(false)) {
      self->pending_script_status = status;
      self->script_pending = true;
      return;
    }
  }

  dispatcher.TryEnqueue([web_view, status] {
    bare_win_ui_web_view__on_ready(
      web_view.get(),
      status,
      L"WebView bridge initialization failed"
    );
  });
}

static void
bare_win_ui_web_view__on_core_ready(
  std::shared_ptr<bare_win_ui_web_view_t> const &web_view,
  AsyncStatus status
) {
  auto self = web_view.get();
  if (self->finalized || self->destroyed) return;

  auto dispatcher = self->dispatcher;

  if (status != AsyncStatus::Completed) {
    dispatcher.TryEnqueue([web_view, status] {
      bare_win_ui_web_view__on_ready(web_view.get(), status);
    });
    return;
  }

  auto core = self->handle.CoreWebView2();
  assert(core);

  try {
    auto registration = core.AddScriptToExecuteOnDocumentCreatedAsync(
      hstring(bare_win_ui_web_view__bridge_script)
    );
    registration.Completed([web_view, dispatcher](auto &, auto &status) {
      bare_win_ui_web_view__on_script_ready(web_view, status, dispatcher);
    });
  } catch (hresult_error const &) {
    dispatcher.TryEnqueue([web_view] {
      bare_win_ui_web_view__on_ready(
        web_view.get(),
        AsyncStatus::Error,
        L"WebView bridge initialization failed"
      );
    });
  }
}

static js_value_t *
bare_win_ui_web_view_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 3;
  js_value_t *argv[3];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 3);

  auto web_view = std::make_shared<bare_win_ui_web_view_t>();

  web_view->env = env;

  err = js_create_reference(env, argv[0], 1, &web_view->ctx);
  assert(err == 0);

  err = js_create_reference(env, argv[1], 1, &web_view->on_ready);
  assert(err == 0);

  err = js_create_reference(env, argv[2], 1, &web_view->on_message);
  assert(err == 0);

  web_view->owner = web_view;
  auto owner = new std::shared_ptr<bare_win_ui_web_view_t>(web_view);

  js_value_t *result;
  err = js_create_external(env, web_view.get(), bare_win_ui_web_view__on_release, owner, &result);
  assert(err == 0);

  auto req = web_view->handle.EnsureCoreWebView2Async();

  DispatcherQueue dispatcher = DispatcherQueue::GetForCurrentThread();
  web_view->dispatcher = dispatcher;

  req.Completed([web_view](auto &, auto &status) {
    auto completion = status;

    {
      std::lock_guard guard(web_view->ready_lock);
      if (bare_win_ui_web_view__test_hold_ready.exchange(false)) {
        web_view->pending_ready_status = completion;
        web_view->ready_pending = true;
        return;
      }
    }

    bare_win_ui_web_view__on_core_ready(web_view, completion);
  });

  return result;
}

static js_value_t *
bare_win_ui_web_view_test_hold_ready(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 0;
  err = js_get_callback_info(env, info, &argc, nullptr, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 0);

  bare_win_ui_web_view__test_hold_ready.store(true);
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_ready_pending(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  bool pending;
  {
    std::lock_guard guard(web_view->ready_lock);
    pending = web_view->ready_pending;
  }

  js_value_t *result;
  err = js_get_boolean(env, pending, &result);
  assert(err == 0);
  return result;
}

static js_value_t *
bare_win_ui_web_view_test_message(js_env_t *env, js_callback_info_t *info) {
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
  err = js_get_value_string_utf16le(
    env,
    argv[1],
    reinterpret_cast<utf16_t *>(message.data()),
    len,
    nullptr
  );
  assert(err == 0);

  bare_win_ui_web_view__on_message(web_view, hstring(message.data(), len));
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_release_ready(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  AsyncStatus status;
  {
    std::lock_guard guard(web_view->ready_lock);
    if (!web_view->ready_pending) return nullptr;
    status = web_view->pending_ready_status;
    web_view->ready_pending = false;
  }

  auto owner = web_view->owner.lock();
  if (owner != nullptr) {
    bare_win_ui_web_view__on_core_ready(owner, status);
  }
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_hold_script(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 0;
  err = js_get_callback_info(env, info, &argc, nullptr, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 0);

  bare_win_ui_web_view__test_hold_script.store(true);
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_fail_script(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 0;
  err = js_get_callback_info(env, info, &argc, nullptr, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 0);

  bare_win_ui_web_view__test_fail_script.store(true);
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_script_pending(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  bool pending;
  {
    std::lock_guard guard(web_view->script_lock);
    pending = web_view->script_pending;
  }

  js_value_t *result;
  err = js_get_boolean(env, pending, &result);
  assert(err == 0);
  return result;
}

static js_value_t *
bare_win_ui_web_view_test_release_script(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_web_view_t *web_view;
  err = js_get_value_external(env, argv[0], (void **) &web_view);
  assert(err == 0);

  AsyncStatus status;
  {
    std::lock_guard guard(web_view->script_lock);
    if (!web_view->script_pending) return nullptr;
    status = web_view->pending_script_status;
    web_view->script_pending = false;
  }

  bare_win_ui_web_view__on_ready(
    web_view,
    status,
    L"WebView bridge initialization failed"
  );
  return nullptr;
}

static js_value_t *
bare_win_ui_web_view_test_non_string_message(js_env_t *env, js_callback_info_t *info) {
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

  core.NavigateToString(hstring(LR"HTML(
<!doctype html>
<script>
  window.chrome.webview.postMessage({ nonString: true })
</script>
)HTML"));

  return nullptr;
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
