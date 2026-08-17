#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <assert.h>
#include <bare.h>
#include <io.h>
#include <js.h>
#include <path.h>
#include <rlimit.h>
#include <uv.h>

#include "bootstrap.h"
#include "windows-app-sdk.h"

static uv_barrier_t bare__platform_ready;
static uv_async_t bare__platform_shutdown;
static uv_thread_t bare__platform_thread;
static js_platform_t *bare__platform;

static uv_loop_t *bare__loop;
static uv_async_t bare__shutdown;
static bare_t *bare;

static DispatcherQueue bare__dispatcher = nullptr;
static std::atomic<bool> bare__running = true;
static std::atomic<bool> bare__terminating = false;
static std::thread bare__poller;

static void
bare__on_shutdown(uv_async_t *handle) {
  uv_close(reinterpret_cast<uv_handle_t *>(handle), nullptr);
}

static void
bare__on_platform_thread(void *data) {
  int err;

  uv_loop_t loop;
  err = uv_loop_init(&loop);
  assert(err == 0);

  err = uv_async_init(&loop, &bare__platform_shutdown, bare__on_shutdown);
  assert(err == 0);

  err = js_create_platform(&loop, nullptr, &bare__platform);
  assert(err == 0);

  uv_barrier_wait(&bare__platform_ready);

  err = uv_run(&loop, UV_RUN_DEFAULT);
  assert(err == 0);

  err = js_destroy_platform(bare__platform);
  assert(err == 0);

  err = uv_run(&loop, UV_RUN_DEFAULT);
  assert(err == 0);

  err = uv_loop_close(&loop);
  assert(err == 0);
}

static void
bare__on_poller_thread(void) {
  std::mutex lock;
  std::condition_variable condition;

  std::unique_lock guard(lock);

  while (bare__running) {
    auto pending = true;
    int timeout;

    bare__dispatcher.TryEnqueue([&]() {
      int err;

      std::unique_lock guard(lock);

      pending = false;

      err = bare_run(bare, UV_RUN_NOWAIT);
      assert(err >= 0);

      timeout = uv_backend_timeout(bare__loop);

      condition.notify_one();
    });

    condition.wait(guard, [&] { return pending == false; });

    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED *overlapped;

    GetQueuedCompletionStatus(bare__loop->iocp, &bytes, &key, &overlapped, timeout);

    if (overlapped) {
      PostQueuedCompletionStatus(bare__loop->iocp, bytes, key, overlapped);
    }
  }
}

static void
bare__terminate(void) {
  bool expected = false;
  if (!bare__terminating.compare_exchange_strong(expected, true)) return;

  int err;

  err = uv_async_send(&bare__shutdown);
  assert(err == 0);

  err = bare_terminate(bare);
  assert(err == 0);

  bare__running = false;

  PostQueuedCompletionStatus(bare__loop->iocp, 0, 0, nullptr);

  bare__poller.join();

  err = bare_run(bare, UV_RUN_DEFAULT);
  assert(err == 0);

  int exit_code;
  err = bare_teardown(bare, UV_RUN_DEFAULT, &exit_code);
  assert(err == 0);

  err = uv_loop_close(bare__loop);
  assert(err == 0);

  err = uv_async_send(&bare__platform_shutdown);
  assert(err == 0);

  uv_thread_join(&bare__platform_thread);

  if (exit_code != 0) _exit(exit_code);
}

static void
bare__launch() {
  int err;

  size_t len;

  char bin[4096];
  len = sizeof(bin);

  err = uv_exepath(bin, &len);
  assert(err == 0);

  char entry[4096];
  len = 4096;

  err = path_join(
    (const char *[]) {bin, "..", "app.bundle", nullptr},
    entry,
    &len,
    path_behavior_system
  );
  assert(err == 0);

  err = bare_load(bare, entry, nullptr, nullptr);
  (void) err;

  bare__dispatcher = DispatcherQueue::GetForCurrentThread();

  bare__dispatcher.ShutdownCompleted([=](const auto &, const auto &) {
    bare__terminate();
  });

  bare__poller = std::thread([]() {
    bare__on_poller_thread();
  });
}

struct BareApp : public ApplicationT<BareApp> {
  void
  OnLaunched(LaunchActivatedEventArgs const &) {
    bare__launch();
  }
};

int
main(int argc, char *argv[]) {
  int err;

  err = rlimit_set(rlimit_open_files, rlimit_infer);
  assert(err == 0);

  freopen("NUL", "r", stdin);
  freopen("NUL", "w", stdout);
  freopen("NUL", "w", stderr);

  uv_disable_stdio_inheritance();

  argv = uv_setup_args(argc, argv);

  err = uv_barrier_init(&bare__platform_ready, 2);
  assert(err == 0);

  err = uv_thread_create(&bare__platform_thread, bare__on_platform_thread, nullptr);
  assert(err == 0);

  uv_barrier_wait(&bare__platform_ready);

  uv_barrier_destroy(&bare__platform_ready);

  init_apartment(apartment_type::single_threaded);

  bare__loop = uv_default_loop();

  size_t len;

  char bin[4096];
  len = sizeof(bin);

  err = uv_exepath(bin, &len);
  assert(err == 0);

  char preflight[4096];
  len = 4096;

  err = path_join(
    (const char *[]) {bin, "..", "preflight.bundle", nullptr},
    preflight,
    &len,
    path_behavior_system
  );
  assert(err == 0);

  uv_fs_t fs;
  err = uv_fs_access(bare__loop, &fs, preflight, R_OK, nullptr);

  if (err == 0) {
    err = bare_setup(bare__loop, bare__platform, nullptr, argc, const_cast<const char **>(argv), nullptr, &bare);
    assert(err == 0);

    err = bare_load(bare, preflight, nullptr, nullptr);
    (void) err;

    err = bare_run(bare, UV_RUN_DEFAULT);
    assert(err == 0);

    int exit_code;
    err = bare_teardown(bare, UV_RUN_DEFAULT, &exit_code);
    assert(err == 0);

    if (exit_code != 0) _exit(exit_code);
  }

  err = uv_async_init(bare__loop, &bare__shutdown, bare__on_shutdown);
  assert(err == 0);

  err = bare_setup(bare__loop, bare__platform, nullptr, argc, const_cast<const char **>(argv), nullptr, &bare);
  assert(err == 0);

  bare__try_bootstrap_runtime();

  Application::Start([=](auto &&) { make<BareApp>(); });

  bare__terminate();

  return 0;
}
