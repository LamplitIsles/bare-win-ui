#include <assert.h>
#include <atomic>
#include <bare.h>
#include <js.h>
#include <shellapi.h>
#include <utf.h>
#include <windowsx.h>

#include <cwchar>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "icon.h"
#include "windows-app-sdk.h"

struct bare_win_ui_notification_item_t {
  std::wstring id;
  std::wstring title;
  UINT command;
  bool separator;
  bool enabled;
};

struct bare_win_ui_notification_area_t {
  js_env_t *env;
  js_ref_t *ctx;
  js_ref_t *on_select;

  HWND window = nullptr;
  HMENU menu = nullptr;
  HICON icon = nullptr;
  bool icon_owned = false;
  UINT taskbar_created = 0;
  UINT next_command = 1;
  bool icon_added = false;
  bool destroyed = false;
  bool references_deleted = false;
  const char *native_error = nullptr;

  std::wstring tooltip;
  std::vector<bare_win_ui_notification_item_t> items;
  std::unordered_map<UINT, std::wstring> commands;
};

static std::atomic<int32_t> bare_win_ui_notification__test_live_resources = 0;
static std::atomic<bool> bare_win_ui_notification__test_fail_init = false;

static constexpr UINT bare_win_ui_notification__message = WM_APP + 1;
static constexpr wchar_t bare_win_ui_notification__class_name[] = L"BareWinUINotificationArea";

static LRESULT CALLBACK
bare_win_ui_notification__window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

static bool
bare_win_ui_notification__ensure_class() {
  static ATOM atom = 0;
  if (atom != 0) return true;

  WNDCLASSEXW klass = {};
  klass.cbSize = sizeof(klass);
  klass.lpfnWndProc = bare_win_ui_notification__window_proc;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = bare_win_ui_notification__class_name;

  atom = RegisterClassExW(&klass);
  return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static HICON
bare_win_ui_notification__load_icon(bool &owned) {
  auto packaged = bare_win_ui_load_packaged_icon(
    GetSystemMetrics(SM_CXSMICON),
    GetSystemMetrics(SM_CYSMICON)
  );
  if (packaged != nullptr) {
    owned = true;
    return packaged;
  }

  owned = false;
  std::vector<wchar_t> path(MAX_PATH);
  for (;;) {
    DWORD length = GetModuleFileNameW(
      nullptr,
      path.data(),
      static_cast<DWORD>(path.size())
    );
    if (length == 0) break;
    if (length < path.size()) {
      HICON icon = nullptr;
      if (ExtractIconExW(path.data(), 0, &icon, nullptr, 1) == 1 && icon != nullptr) {
        owned = true;
        return icon;
      }
      break;
    }
    path.resize(path.size() * 2);
  }

  return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

static bool
bare_win_ui_notification__test_should_fail_init() {
  return bare_win_ui_notification__test_fail_init.exchange(false);
}

static bool
bare_win_ui_notification__set_icon(bare_win_ui_notification_area_t *self) {
  NOTIFYICONDATAW data = {};
  data.cbSize = sizeof(data);
  data.hWnd = self->window;
  data.uID = 1;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = bare_win_ui_notification__message;
  data.uVersion = NOTIFYICON_VERSION_4;
  data.hIcon = self->icon;
  wcsncpy_s(
    data.szTip,
    ARRAYSIZE(data.szTip),
    self->tooltip.c_str(),
    _TRUNCATE
  );

  if (!Shell_NotifyIconW(NIM_ADD, &data)) {
    self->native_error = "could not add notification area icon";
    return false;
  }

  if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
    Shell_NotifyIconW(NIM_DELETE, &data);
    self->native_error = "could not set notification area icon version";
    return false;
  }

  self->icon_added = true;
  return true;
}

static bool
bare_win_ui_notification__rebuild_menu(bare_win_ui_notification_area_t *self) {
  auto menu = CreatePopupMenu();
  if (menu == nullptr) {
    self->native_error = "could not create notification area menu";
    return false;
  }
  bare_win_ui_notification__test_live_resources++;

  std::unordered_map<UINT, std::wstring> commands;

  for (auto &item : self->items) {
    if (item.separator) {
      if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)) {
        DestroyMenu(menu);
        bare_win_ui_notification__test_live_resources--;
        self->native_error = "could not add notification area separator";
        return false;
      }
      continue;
    }

    item.command = self->next_command++;
    commands[item.command] = item.id;

    UINT flags = MF_STRING;
    if (!item.enabled) flags |= MF_GRAYED;

    if (!AppendMenuW(menu, flags, item.command, item.title.c_str())) {
      DestroyMenu(menu);
      bare_win_ui_notification__test_live_resources--;
      self->native_error = "could not add notification area item";
      return false;
    }
  }

  if (self->menu != nullptr) {
    if (!DestroyMenu(self->menu)) {
      DestroyMenu(menu);
      bare_win_ui_notification__test_live_resources--;
      self->native_error = "could not replace notification area menu";
      return false;
    }
    bare_win_ui_notification__test_live_resources--;
  }

  self->menu = menu;
  self->commands = std::move(commands);
  return true;
}

static bool
bare_win_ui_notification__can_mutate(js_env_t *env, bare_win_ui_notification_area_t *self) {
  if (self->destroyed) return false;

  if (self->native_error != nullptr) {
    js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
    return false;
  }

  return true;
}

static void
bare_win_ui_notification__show_menu(
  bare_win_ui_notification_area_t *self,
  POINT point
) {
  if (self->destroyed || self->menu == nullptr) return;

  if (!SetForegroundWindow(self->window)) {
    self->native_error = "could not activate notification area menu";
    return;
  }

  if (!TrackPopupMenu(
    self->menu,
    TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
    point.x,
    point.y,
    0,
    self->window,
    nullptr
  )) {
    self->native_error = "could not show notification area menu";
    return;
  }

  if (!PostMessageW(self->window, WM_NULL, 0, 0)) {
    self->native_error = "could not complete notification area menu";
    return;
  }

  NOTIFYICONDATAW focus = {};
  focus.cbSize = sizeof(focus);
  focus.hWnd = self->window;
  focus.uID = 1;
  Shell_NotifyIconW(NIM_SETFOCUS, &focus);
}

static void
bare_win_ui_notification__on_select(bare_win_ui_notification_area_t *self, UINT command) {
  int err;

  auto found = self->commands.find(command);
  if (found == self->commands.end() || self->destroyed) return;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(self->env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(self->env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_select;
  err = js_get_reference_value(self->env, self->on_select, &on_select);
  assert(err == 0);

  js_value_t *args[1];
  auto const &id = found->second;
  err = js_create_string_utf16le(
    self->env,
    reinterpret_cast<const utf16_t *>(id.data()),
    id.size(),
    &args[0]
  );
  assert(err == 0);

  err = js_call_function(self->env, ctx, on_select, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(self->env, scope);
  assert(err == 0);
}

static bool
bare_win_ui_notification__destroy(bare_win_ui_notification_area_t *self) {
  if (self->destroyed) return self->native_error == nullptr;

  self->destroyed = true;

  if (self->icon_added) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = self->window;
    data.uID = 1;
    if (!Shell_NotifyIconW(NIM_DELETE, &data) && self->native_error == nullptr) {
      self->native_error = "could not remove notification area icon";
    }
    self->icon_added = false;
  }

  if (self->menu != nullptr) {
    if (!DestroyMenu(self->menu) && self->native_error == nullptr) {
      self->native_error = "could not destroy notification area menu";
    }
    bare_win_ui_notification__test_live_resources--;
    self->menu = nullptr;
  }

  if (self->window != nullptr) {
    if (!DestroyWindow(self->window) && self->native_error == nullptr) {
      self->native_error = "could not destroy notification area window";
    }
    bare_win_ui_notification__test_live_resources--;
    self->window = nullptr;
  }

  if (self->icon != nullptr) {
    if (self->icon_owned && !DestroyIcon(self->icon) && self->native_error == nullptr) {
      self->native_error = "could not destroy notification area icon";
    }
    bare_win_ui_notification__test_live_resources--;
  }
  self->icon = nullptr;
  self->icon_owned = false;

  return self->native_error == nullptr;
}

static LRESULT CALLBACK
bare_win_ui_notification__window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto self = reinterpret_cast<bare_win_ui_notification_area_t *>(
    GetWindowLongPtrW(hwnd, GWLP_USERDATA)
  );

  if (message == WM_NCCREATE) {
    auto create = reinterpret_cast<CREATESTRUCTW *>(lparam);
    self = reinterpret_cast<bare_win_ui_notification_area_t *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);

  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
  }

  if (message == self->taskbar_created) {
    if (self->destroyed) return 0;

    self->icon_added = false;
    bare_win_ui_notification__set_icon(self);
    return 0;
  }

  if (message == bare_win_ui_notification__message) {
    auto event = LOWORD(lparam);
    auto icon_id = HIWORD(lparam);
    if (icon_id != 1) return 0;

    if (
      event == WM_LBUTTONUP ||
      event == WM_RBUTTONUP ||
      event == WM_CONTEXTMENU ||
      event == NIN_SELECT ||
      event == NIN_KEYSELECT
    ) {
      POINT point = {GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
      bare_win_ui_notification__show_menu(self, point);
    }
    return 0;
  }

  if (message == WM_COMMAND && HIWORD(wparam) == 0) {
    bare_win_ui_notification__on_select(self, LOWORD(wparam));
    return 0;
  }

  return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void
bare_win_ui_notification_area__delete_references(bare_win_ui_notification_area_t *self) {
  int err;

  if (self->references_deleted) return;

  self->references_deleted = true;

  err = js_delete_reference(self->env, self->on_select);
  assert(err == 0);

  err = js_delete_reference(self->env, self->ctx);
  assert(err == 0);
}

static void
bare_win_ui_notification_area__on_release(js_env_t *env, void *data, void *finalize_hint) {
  auto self = reinterpret_cast<bare_win_ui_notification_area_t *>(data);
  bare_win_ui_notification__destroy(self);
  bare_win_ui_notification_area__delete_references(self);
  delete self;
}

static js_value_t *
bare_win_ui_notification_area_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 3;
  js_value_t *argv[3];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 3);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> tooltip(len);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(tooltip.data()), len, nullptr);
  assert(err == 0);

  auto self = new bare_win_ui_notification_area_t();
  self->env = env;
  self->tooltip.assign(tooltip.data(), len);

  err = js_create_reference(env, argv[0], 1, &self->ctx);
  assert(err == 0);
  err = js_create_reference(env, argv[2], 1, &self->on_select);
  assert(err == 0);

  self->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  if (!bare_win_ui_notification__ensure_class() || self->taskbar_created == 0) {
    js_throw_error(env, "ERR_NATIVE_SETUP", "could not register notification area window");
    js_delete_reference(env, self->on_select);
    js_delete_reference(env, self->ctx);
    delete self;
    return nullptr;
  }

  self->window = CreateWindowExW(
    WS_EX_TOOLWINDOW,
    bare_win_ui_notification__class_name,
    L"Bare WinUI notification area",
    WS_POPUP,
    0,
    0,
    0,
    0,
    nullptr,
    nullptr,
    GetModuleHandleW(nullptr),
    self
  );

  if (self->window == nullptr) {
    js_throw_error(env, "ERR_NATIVE_SETUP", "could not create notification area window");
    js_delete_reference(env, self->on_select);
    js_delete_reference(env, self->ctx);
    delete self;
    return nullptr;
  }
  bare_win_ui_notification__test_live_resources++;
  self->icon = bare_win_ui_notification__load_icon(self->icon_owned);
  if (self->icon != nullptr) bare_win_ui_notification__test_live_resources++;
  if (self->icon == nullptr || !bare_win_ui_notification__set_icon(self)) {
    js_throw_error(env, "ERR_NATIVE_SETUP", "could not add notification area icon");
    bare_win_ui_notification__destroy(self);
    js_delete_reference(env, self->on_select);
    js_delete_reference(env, self->ctx);
    delete self;
    return nullptr;
  }

  if (!bare_win_ui_notification__rebuild_menu(self)) {
    js_throw_error(env, "ERR_NATIVE_SETUP", self->native_error);
    bare_win_ui_notification__destroy(self);
    js_delete_reference(env, self->on_select);
    js_delete_reference(env, self->ctx);
    delete self;
    return nullptr;
  }

  if (bare_win_ui_notification__test_should_fail_init()) {
    js_throw_error(env, "ERR_NATIVE_SETUP", "test notification area failure after menu");
    bare_win_ui_notification__destroy(self);
    js_delete_reference(env, self->on_select);
    js_delete_reference(env, self->ctx);
    delete self;
    return nullptr;
  }

  js_value_t *result;
  err = js_create_external(env, self, bare_win_ui_notification_area__on_release, nullptr, &result);
  assert(err == 0);
  return result;
}

static bool
bare_win_ui_notification__get_string(js_env_t *env, js_value_t *value, std::wstring &result) {
  size_t len;
  int err = js_get_value_string_utf16le(env, value, nullptr, 0, &len);
  if (err != 0) return false;

  std::vector<wchar_t> string(len);
  err = js_get_value_string_utf16le(env, value, reinterpret_cast<utf16_t *>(string.data()), len, nullptr);
  if (err != 0) return false;

  result.assign(string.data(), len);
  return true;
}

static js_value_t *
bare_win_ui_notification_area_add_item(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 4;
  js_value_t *argv[4];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 4);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (!bare_win_ui_notification__can_mutate(env, self)) return nullptr;

  bare_win_ui_notification_item_t item;
  item.separator = false;
  item.command = 0;
  err = js_get_value_bool(env, argv[3], &item.enabled);
  assert(err == 0);
  if (!bare_win_ui_notification__get_string(env, argv[1], item.id) ||
      !bare_win_ui_notification__get_string(env, argv[2], item.title)) {
    js_throw_error(env, "ERR_INVALID_ARGUMENT", "notification item strings are required");
    return nullptr;
  }

  self->items.push_back(std::move(item));
  if (!bare_win_ui_notification__rebuild_menu(self)) {
    js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
  }
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_add_separator(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (!bare_win_ui_notification__can_mutate(env, self)) return nullptr;

  self->items.push_back({L"", L"", 0, true, false});
  if (!bare_win_ui_notification__rebuild_menu(self)) {
    js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
  }
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_update_item(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 4;
  js_value_t *argv[4];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 4);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (!bare_win_ui_notification__can_mutate(env, self)) return nullptr;

  std::wstring id;
  std::wstring title;
  bool enabled;
  err = js_get_value_bool(env, argv[3], &enabled);
  assert(err == 0);
  if (!bare_win_ui_notification__get_string(env, argv[1], id) ||
      !bare_win_ui_notification__get_string(env, argv[2], title)) {
    js_throw_error(env, "ERR_INVALID_ARGUMENT", "notification item strings are required");
    return nullptr;
  }

  for (auto &item : self->items) {
    if (!item.separator && item.id == id) {
      item.title = title;
      item.enabled = enabled;
      if (!bare_win_ui_notification__rebuild_menu(self)) {
        js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
      }
      return nullptr;
    }
  }

  js_throw_error(env, "ERR_NOT_FOUND", "notification item does not exist");
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_remove_item(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 2;
  js_value_t *argv[2];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 2);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (!bare_win_ui_notification__can_mutate(env, self)) return nullptr;

  std::wstring id;
  if (!bare_win_ui_notification__get_string(env, argv[1], id)) {
    js_throw_error(env, "ERR_INVALID_ARGUMENT", "notification item ID is required");
    return nullptr;
  }

  for (auto it = self->items.begin(); it != self->items.end(); ++it) {
    if (!it->separator && it->id == id) {
      self->items.erase(it);
      if (!bare_win_ui_notification__rebuild_menu(self)) {
        js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
      }
      return nullptr;
    }
  }

  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_clear(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (!bare_win_ui_notification__can_mutate(env, self)) return nullptr;

  self->items.clear();
  if (!bare_win_ui_notification__rebuild_menu(self)) {
    js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
  }
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_test_fail_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 0;
  err = js_get_callback_info(env, info, &argc, nullptr, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 0);

  bare_win_ui_notification__test_fail_init.store(true);
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_test_live_resources(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 0;
  err = js_get_callback_info(env, info, &argc, nullptr, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 0);

  js_value_t *result;
  err = js_create_int32(env, bare_win_ui_notification__test_live_resources.load(), &result);
  assert(err == 0);
  return result;
}

static js_value_t *
bare_win_ui_notification_area_test_select(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 2;
  js_value_t *argv[2];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 2);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (self->destroyed) return nullptr;

  std::wstring id;
  if (!bare_win_ui_notification__get_string(env, argv[1], id)) {
    js_throw_error(env, "ERR_INVALID_ARGUMENT", "notification item ID is required");
    return nullptr;
  }

  for (auto const &[command, item_id] : self->commands) {
    if (item_id == id) {
      SendMessageW(self->window, WM_COMMAND, command, 0);
      return nullptr;
    }
  }

  js_throw_error(env, "ERR_NOT_FOUND", "notification item does not exist");
  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_test_taskbar_created(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  if (self->destroyed) return nullptr;

  NOTIFYICONDATAW data = {};
  data.cbSize = sizeof(data);
  data.hWnd = self->window;
  data.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &data);
  self->icon_added = false;

  SendMessageW(self->window, self->taskbar_created, 0, 0);
  if (!self->icon_added) {
    js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
  }

  return nullptr;
}

static js_value_t *
bare_win_ui_notification_area_destroy(js_env_t *env, js_callback_info_t *info) {
  int err;
  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  bare_win_ui_notification_area_t *self;
  err = js_get_value_external(env, argv[0], (void **) &self);
  assert(err == 0);

  auto ok = bare_win_ui_notification__destroy(self);
  bare_win_ui_notification_area__delete_references(self);

  if (!ok) js_throw_error(env, "ERR_NATIVE_OPERATION", self->native_error);
  return nullptr;
}
