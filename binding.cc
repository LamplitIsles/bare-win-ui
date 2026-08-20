#include <assert.h>
#include <bare.h>
#include <js.h>
#include <utf.h>

#include <windows.h>

#include <vector>

#include "lib/notification-area.h"
#include "lib/package-manager.h"
#include "lib/web-view.h"
#include "lib/window.h"

#ifdef BARE_WIN_UI_TESTING
static js_value_t *
bare_win_ui_application_test_mark_constructed(
  js_env_t *env,
  js_callback_info_t *info
) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];
  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);
  assert(argc == 1);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[0], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> path(len + 1);
  err = js_get_value_string_utf16le(
    env,
    argv[0],
    reinterpret_cast<utf16_t *>(path.data()),
    len,
    nullptr
  );
  assert(err == 0);
  path[len] = L'\0';

  HANDLE marker = CreateFileW(
    path.data(),
    GENERIC_WRITE,
    0,
    nullptr,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );
  if (marker == INVALID_HANDLE_VALUE) {
    js_throw_error(
      env,
      "ERR_TEST_MARKER",
      "could not create application construction marker"
    );
    return nullptr;
  }

  constexpr char contents[] = "constructed\n";
  DWORD written;
  BOOL success = WriteFile(marker, contents, sizeof(contents) - 1, &written, nullptr);
  BOOL closed = CloseHandle(marker);
  if (!success || !closed || written != sizeof(contents) - 1) {
    js_throw_error(
      env,
      "ERR_TEST_MARKER",
      "could not write application construction marker"
    );
  }

  return nullptr;
}
#endif

static js_value_t *
bare_win_ui_exports(js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, nullptr, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("packageManagerInit", bare_win_ui_package_manager_init)
  V("packageManagerAddPackage", bare_win_ui_package_manager_add_package)

  V("windowInit", bare_win_ui_window_init)
  V("windowTitle", bare_win_ui_window_title)
  V("windowContent", bare_win_ui_window_content)
  V("windowActivate", bare_win_ui_window_activate)
  V("windowHide", bare_win_ui_window_hide)
  V("windowClose", bare_win_ui_window_close)
  V("windowTestCloseRequest", bare_win_ui_window_test_close_request)
#ifdef BARE_WIN_UI_TESTING
  V("applicationTestMarkConstructed", bare_win_ui_application_test_mark_constructed)
#endif
  V("windowResize", bare_win_ui_window_resize)
  V("windowResizeClient", bare_win_ui_window_resize_client)

  V("webViewInit", bare_win_ui_web_view_init)
  V("webViewTestHoldReady", bare_win_ui_web_view_test_hold_ready)
  V("webViewTestReadyPending", bare_win_ui_web_view_test_ready_pending)
  V("webViewTestMessage", bare_win_ui_web_view_test_message)
  V("webViewTestReleaseReady", bare_win_ui_web_view_test_release_ready)
#ifdef BARE_WIN_UI_TESTING
  V("webViewTestHoldScript", bare_win_ui_web_view_test_hold_script)
  V("webViewTestFailScript", bare_win_ui_web_view_test_fail_script)
  V("webViewTestScriptPending", bare_win_ui_web_view_test_script_pending)
  V("webViewTestReleaseScript", bare_win_ui_web_view_test_release_script)
  V("webViewTestNavigationStarted", bare_win_ui_web_view_test_navigation_started)
  V("webViewTestNonStringMessage", bare_win_ui_web_view_test_non_string_message)
  V("webViewTestResetPostMessageCount", bare_win_ui_web_view_test_reset_post_message_count)
  V("webViewTestPostMessageCount", bare_win_ui_web_view_test_post_message_count)
#endif
  V("webViewWidth", bare_win_ui_web_view_width)
  V("webViewHeight", bare_win_ui_web_view_height)
  V("webViewSource", bare_win_ui_web_view_source)
  V("webViewNavigate", bare_win_ui_web_view_navigate)
  V("webViewNavigateToString", bare_win_ui_web_view_navigate_to_string)
  V("webViewPostMessage", bare_win_ui_web_view_post_message)
  V("webViewOpenExternal", bare_win_ui_web_view_open_external)
  V("webViewOpenDevToolsWindow", bare_win_ui_web_view_open_dev_tools_window)
  V("webViewDestroy", bare_win_ui_web_view_destroy)

  V("notificationAreaInit", bare_win_ui_notification_area_init)
  V("notificationAreaAddItem", bare_win_ui_notification_area_add_item)
  V("notificationAreaAddSeparator", bare_win_ui_notification_area_add_separator)
  V("notificationAreaUpdateItem", bare_win_ui_notification_area_update_item)
  V("notificationAreaRemoveItem", bare_win_ui_notification_area_remove_item)
  V("notificationAreaClear", bare_win_ui_notification_area_clear)
  V("notificationAreaTestFailInit", bare_win_ui_notification_area_test_fail_init)
  V("notificationAreaTestLiveResources", bare_win_ui_notification_area_test_live_resources)
  V("notificationAreaTestSelect", bare_win_ui_notification_area_test_select)
  V("notificationAreaTestTaskbarCreated", bare_win_ui_notification_area_test_taskbar_created)
  V("notificationAreaDestroy", bare_win_ui_notification_area_destroy)
#undef V

  return exports;
}

BARE_MODULE(bare_win_ui, bare_win_ui_exports)
