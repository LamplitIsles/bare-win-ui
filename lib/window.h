#include <assert.h>
#include <bare.h>
#include <js.h>
#include <winuser.h>

#include <memory>

#include "element.h"
#include "icon.h"
#include "windows-app-sdk.h"

struct bare_win_ui_window_t {
  Window handle;

  js_env_t *env;
  js_ref_t *ctx;
  js_ref_t *on_closing;
  js_ref_t *on_close;
  js_ref_t *on_resize;

  event_token closing_token;
  event_token closed_token;
  event_token size_changed_token;
  bool events_revoked = false;
  bool programmatic_close = false;
  bool closed = false;
  bool finalized = false;
  bool references_deleted = false;
  HICON large_icon = nullptr;
  HICON small_icon = nullptr;
  float last_width = -1;
  float last_height = -1;
};

static void
bare_win_ui_window__delete_references(bare_win_ui_window_t *self) {
  int err;

  if (self->references_deleted) return;

  self->references_deleted = true;

  err = js_delete_reference(self->env, self->on_resize);
  assert(err == 0);

  err = js_delete_reference(self->env, self->on_close);
  assert(err == 0);

  err = js_delete_reference(self->env, self->on_closing);
  assert(err == 0);

  err = js_delete_reference(self->env, self->ctx);
  assert(err == 0);
}

static void
bare_win_ui_window__revoke_events(bare_win_ui_window_t *self) {
  if (self->events_revoked) return;

  self->events_revoked = true;

  self->handle.AppWindow().Closing(self->closing_token);
  self->handle.Closed(self->closed_token);
  self->handle.SizeChanged(self->size_changed_token);
}

static void
bare_win_ui_window__destroy_icons(bare_win_ui_window_t *self) {
  auto hwnd = GetWindowFromWindowId(self->handle.AppWindow().Id());
  if (hwnd != nullptr) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);
  }

  if (self->large_icon != nullptr) {
    DestroyIcon(self->large_icon);
    self->large_icon = nullptr;
  }
  if (self->small_icon != nullptr) {
    DestroyIcon(self->small_icon);
    self->small_icon = nullptr;
  }
}

static void
bare_win_ui_window__on_release(js_env_t *env, void *data, void *finalize_hint) {
  auto owner = reinterpret_cast<std::shared_ptr<bare_win_ui_window_t> *>(finalize_hint);
  auto self = owner != nullptr ? owner->get() : reinterpret_cast<bare_win_ui_window_t *>(data);

  if (self != nullptr) {
    auto was_closed = self->closed;

    self->finalized = true;
    self->closed = true;
    self->programmatic_close = true;
    bare_win_ui_window__revoke_events(self);
    bare_win_ui_window__destroy_icons(self);
    if (!was_closed) self->handle.Close();
    bare_win_ui_window__delete_references(self);
  }

  if (owner != nullptr) {
    owner->reset();
    delete owner;
  }
}

static bool
bare_win_ui_window__on_closing(bare_win_ui_window_t *self) {
  int err;

  if (self->finalized || self->closed || self->programmatic_close) return false;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(self->env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(self->env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_closing;
  err = js_get_reference_value(self->env, self->on_closing, &on_closing);
  assert(err == 0);

  js_value_t *result;
  err = js_call_function(self->env, ctx, on_closing, 0, nullptr, &result);

  bool suppress = false;
  if (err == 0) {
    err = js_get_value_bool(self->env, result, &suppress);
    if (err != 0) suppress = false;
  }

  err = js_close_handle_scope(self->env, scope);
  assert(err == 0);

  return suppress;
}

static void
bare_win_ui_window__on_close(bare_win_ui_window_t *self) {
  int err;

  if (self->finalized || self->closed) return;

  self->closed = true;
  bare_win_ui_window__destroy_icons(self);

  js_handle_scope_t *scope;
  err = js_open_handle_scope(self->env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(self->env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_close;
  err = js_get_reference_value(self->env, self->on_close, &on_close);
  assert(err == 0);

  err = js_call_function(self->env, ctx, on_close, 0, nullptr, nullptr);
  (void) err;

  err = js_close_handle_scope(self->env, scope);
  assert(err == 0);

  bare_win_ui_window__delete_references(self);
  bare_win_ui_window__revoke_events(self);
}

static void
bare_win_ui_window__on_resize(bare_win_ui_window_t *self, Size const &size) {
  int err;

  if (self->finalized || self->closed) return;
  if (size.Width == self->last_width && size.Height == self->last_height) return;

  self->last_width = size.Width;
  self->last_height = size.Height;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(self->env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(self->env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_resize;
  err = js_get_reference_value(self->env, self->on_resize, &on_resize);
  assert(err == 0);

  js_value_t *args[2];
  err = js_create_double(self->env, size.Width, &args[0]);
  assert(err == 0);
  err = js_create_double(self->env, size.Height, &args[1]);
  assert(err == 0);

  err = js_call_function(self->env, ctx, on_resize, 2, args, nullptr);
  (void) err;

  err = js_close_handle_scope(self->env, scope);
  assert(err == 0);
}

static inline double
bare_win_ui_window__get_scale(bare_win_ui_window_t *window) {
  auto dpi = GetDpiForWindow(GetWindowFromWindowId(window->handle.AppWindow().Id()));

  return float(dpi) / 96;
}

static js_value_t *
bare_win_ui_window_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 4;
  js_value_t *argv[4];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 4);

  auto window = std::make_shared<bare_win_ui_window_t>();

  window->env = env;

  err = js_create_reference(env, argv[0], 1, &window->ctx);
  assert(err == 0);

  err = js_create_reference(env, argv[1], 1, &window->on_closing);
  assert(err == 0);

  err = js_create_reference(env, argv[2], 1, &window->on_close);
  assert(err == 0);

  err = js_create_reference(env, argv[3], 1, &window->on_resize);
  assert(err == 0);

  window->closing_token = window->handle.AppWindow().Closing([window](auto &, auto &args) {
    args.Cancel(bare_win_ui_window__on_closing(window.get()));
  });

  window->closed_token = window->handle.Closed([window](auto &, auto &) {
    bare_win_ui_window__on_close(window.get());
  });

  window->size_changed_token = window->handle.SizeChanged([window](auto &, auto &args) {
    bare_win_ui_window__on_resize(window.get(), args.Size());
  });

  auto hwnd = GetWindowFromWindowId(window->handle.AppWindow().Id());
  if (hwnd != nullptr) {
    window->large_icon = bare_win_ui_load_packaged_icon(
      GetSystemMetrics(SM_CXICON),
      GetSystemMetrics(SM_CYICON)
    );
    window->small_icon = bare_win_ui_load_packaged_icon(
      GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON)
    );
    if (window->large_icon != nullptr) {
      SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(window->large_icon));
    }
    if (window->small_icon != nullptr) {
      SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(window->small_icon));
    }
  }

  auto owner = new std::shared_ptr<bare_win_ui_window_t>(window);

  js_value_t *result;
  err = js_create_external(env, window.get(), bare_win_ui_window__on_release, owner, &result);
  assert(err == 0);

  return result;
}

static js_value_t *
bare_win_ui_window_title(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1 || argc == 2);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  js_value_t *result = nullptr;

  if (argc == 1) {
    auto title = window->handle.Title();

    err = js_create_string_utf16le(env, reinterpret_cast<const utf16_t *>(title.data()), title.size(), &result);
    assert(err == 0);
  } else {
    size_t len;
    err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
    assert(err == 0);

    std::vector<wchar_t> title(len);
    err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(title.data()), len, nullptr);
    assert(err == 0);

    window->handle.Title(hstring(title.data(), len));
  }

  return result;
}

static js_value_t *
bare_win_ui_window_content(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1 || argc == 2);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  js_value_t *result = nullptr;

  if (argc == 1) {
    auto element = new bare_win_ui_element_t();

    element->handle = window->handle.Content();

    err = js_create_external(env, element, bare_win_ui_element__on_release, nullptr, &result);
    assert(err == 0);
  } else {
    bare_win_ui_element_t *element;
    err = js_get_value_external(env, argv[1], (void **) &element);
    assert(err == 0);

    window->handle.Content(element->handle);
  }

  return result;
}

static js_value_t *
bare_win_ui_window_activate(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  window->handle.Activate();

  return nullptr;
}

static js_value_t *
bare_win_ui_window_hide(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  window->handle.AppWindow().Hide();

  return nullptr;
}

static js_value_t *
bare_win_ui_window_close(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  if (!window->closed) {
    window->programmatic_close = true;
    window->handle.Close();
  }

  return nullptr;
}

static js_value_t *
bare_win_ui_window_test_close_request(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 1);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  if (!window->closed) {
    auto hwnd = GetWindowFromWindowId(window->handle.AppWindow().Id());
    if (hwnd == nullptr) {
      js_throw_error(env, "ERR_NATIVE_OPERATION", "could not find native window");
      return nullptr;
    }

    SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
  }

  return nullptr;
}

static js_value_t *
bare_win_ui_window_resize(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 3;
  js_value_t *argv[3];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 3);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  int32_t width;
  err = js_get_value_int32(env, argv[1], &width);
  assert(err == 0);

  int32_t height;
  err = js_get_value_int32(env, argv[2], &height);
  assert(err == 0);

  auto scale = bare_win_ui_window__get_scale(window);

  width *= scale;
  height *= scale;

  window->handle.AppWindow().Resize({width, height});

  return nullptr;
}

static js_value_t *
bare_win_ui_window_resize_client(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 3;
  js_value_t *argv[3];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 3);

  bare_win_ui_window_t *window;
  err = js_get_value_external(env, argv[0], (void **) &window);
  assert(err == 0);

  int32_t width;
  err = js_get_value_int32(env, argv[1], &width);
  assert(err == 0);

  int32_t height;
  err = js_get_value_int32(env, argv[2], &height);
  assert(err == 0);

  auto scale = bare_win_ui_window__get_scale(window);

  width *= scale;
  height *= scale;

  window->handle.AppWindow().ResizeClient({width, height});

  return nullptr;
}
