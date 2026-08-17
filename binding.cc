#include <assert.h>
#include <bare.h>
#include <js.h>
#include <utf.h>

#include "lib/notification-area.h"
#include "lib/package-manager.h"
#include "lib/web-view.h"
#include "lib/window.h"

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
  V("windowResize", bare_win_ui_window_resize)
  V("windowResizeClient", bare_win_ui_window_resize_client)

  V("webViewInit", bare_win_ui_web_view_init)
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
  V("notificationAreaTestSelect", bare_win_ui_notification_area_test_select)
  V("notificationAreaTestTaskbarCreated", bare_win_ui_notification_area_test_taskbar_created)
  V("notificationAreaDestroy", bare_win_ui_notification_area_destroy)
#undef V

  return exports;
}

BARE_MODULE(bare_win_ui, bare_win_ui_exports)
