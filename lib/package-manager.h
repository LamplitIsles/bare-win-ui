#include <js.h>
#include <utf.h>

#include "windows-app-sdk.h"

struct bare_win_ui_package_manager_t {
  PackageManager handle;
};

struct bare_win_ui_package_manager_add_package_t {
  IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> handle;

  js_env_t *env;
  js_ref_t *ctx;
  js_ref_t *on_progress;
  js_ref_t *on_completed;
};

static void
bare_win_ui_package_manager__on_release(js_env_t *env, void *data, void *finalize_hint) {
  int err;

  auto self = reinterpret_cast<bare_win_ui_package_manager_t *>(data);

  delete self;
}

static js_value_t *
bare_win_ui_package_manager_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  auto package_manager = new bare_win_ui_package_manager_t();

  js_value_t *result;
  err = js_create_external(env, package_manager, bare_win_ui_package_manager__on_release, nullptr, &result);
  assert(err == 0);

  return result;
}

static void
bare_win_ui_package_manager_add_package__on_progress(bare_win_ui_package_manager_add_package_t *self, DeploymentProgress const &progress) {
  int err;

  auto env = self->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_progress;
  err = js_get_reference_value(env, self->on_progress, &on_progress);
  assert(err == 0);

  js_value_t *args[1];

  err = js_create_object(env, &args[0]);
  assert(err == 0);

#define V(name, n) \
  { \
    js_value_t *val; \
    err = js_create_uint32(env, n, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, args[0], name, val); \
    assert(err == 0); \
  }

  V("percentage", progress.percentage);
  V("state", uint32_t(progress.state));
#undef V

  err = js_call_function(env, ctx, on_progress, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static void
bare_win_ui_package_manager_add_package__on_completed(bare_win_ui_package_manager_add_package_t *self, AsyncStatus const &status) {
  int err;

  auto env = self->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *ctx;
  err = js_get_reference_value(env, self->ctx, &ctx);
  assert(err == 0);

  js_value_t *on_completed;
  err = js_get_reference_value(env, self->on_completed, &on_completed);
  assert(err == 0);

  err = js_delete_reference(env, self->on_progress);
  assert(err == 0);

  err = js_delete_reference(env, self->on_completed);
  assert(err == 0);

  err = js_delete_reference(env, self->ctx);
  assert(err == 0);

  js_value_t *args[1];

  auto result = self->handle.GetResults();

  if (status == AsyncStatus::Error) {
    auto error = result.ErrorText();

    err = js_create_string_utf16le(env, reinterpret_cast<const utf16_t *>(error.data()), error.size(), &args[0]);
    assert(err == 0);
  } else {
    err = js_get_null(env, &args[0]);
    assert(err == 0);
  }

  err = js_call_function(env, ctx, on_completed, 1, args, nullptr);
  (void) err;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);

  delete self;
}

static js_value_t *
bare_win_ui_package_manager_add_package(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 5;
  js_value_t *argv[5];

  err = js_get_callback_info(env, info, &argc, argv, nullptr, nullptr);
  assert(err == 0);

  assert(argc == 5);

  bare_win_ui_package_manager_t *package_manager;
  err = js_get_value_external(env, argv[0], (void **) &package_manager);
  assert(err == 0);

  size_t len;
  err = js_get_value_string_utf16le(env, argv[1], nullptr, 0, &len);
  assert(err == 0);

  std::vector<wchar_t> uri(len);
  err = js_get_value_string_utf16le(env, argv[1], reinterpret_cast<utf16_t *>(uri.data()), len, nullptr);
  assert(err == 0);

  auto req = new bare_win_ui_package_manager_add_package_t();

  req->env = env;

  err = js_create_reference(env, argv[2], 1, &req->ctx);
  assert(err == 0);

  err = js_create_reference(env, argv[3], 1, &req->on_progress);
  assert(err == 0);

  err = js_create_reference(env, argv[4], 1, &req->on_completed);
  assert(err == 0);

  js_value_t *result;
  err = js_create_external(env, req, nullptr, nullptr, &result);
  assert(err == 0);

  AddPackageOptions options;

  req->handle = package_manager->handle.AddPackageByUriAsync(Uri(hstring(uri.data(), len)), options);

  DispatcherQueue dispatcher = DispatcherQueue::GetForCurrentThread();

  req->handle.Progress([=](auto &, auto &progress) {
    dispatcher.TryEnqueue([=] {
      bare_win_ui_package_manager_add_package__on_progress(req, progress);
    });
  });

  req->handle.Completed([=](auto &, auto &status) {
    dispatcher.TryEnqueue([=] {
      bare_win_ui_package_manager_add_package__on_completed(req, status);
    });
  });

  return result;
}
