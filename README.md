# bare-win-ui

WinUI bindings and runtime for Bare.

```
npm i bare-win-ui
```

## Primitives

`Window` exposes `activate()`, `show()`, `hide()`, `close()`, title/content,
and resize methods. A user close emits `close-request` with an event object;
calling `preventDefault()` keeps the same native window alive. A final native
close emits `close` once. Size changes emit `resize` with `{ width, height }`.

`WebView` becomes ready asynchronously. `navigate()`, `navigateToString()`,
`postMessage()`, and `openDevToolsWindow()` run after readiness in invocation
order. Page messages are emitted as `message` events. The injected page bridge
matches the portable Bare WebView contract:

```js
window.bareNative.postMessage('from-page')
window.addEventListener('bare-native-message', (event) => {
  console.log(event.data)
})
```

Only strings cross this bridge. `openExternal()` accepts only `http:` and
`https:` URLs and uses the system handler. `destroy()` is idempotent, releases
the native control, and prevents later callbacks.

`NotificationArea` uses the packaged executable's icon when one is available;
otherwise it uses Windows' shared default application icon. It exposes mutable
menu items through stable string IDs:

```js
const tray = new NotificationArea({ tooltip: 'Example' })
tray.addItem('open', 'Open')
tray.addSeparator()
tray.addItem('quit', 'Quit', true)
tray.updateItem('open', 'Open window', true)
tray.on('select', (id) => {})
```

Destroying the notification area removes its icon and permanently disables
its mutators. The icon is re-added when Windows broadcasts the `TaskbarCreated`
restart notification, unless the area has already been destroyed.

A normal lifecycle keeps native teardown explicit:

```js
const window = new Window()
const webview = new WebView()
const tray = new NotificationArea({ tooltip: 'Example' })

window.content = webview
window.on('close-request', (event) => {
  webview.destroy()
  tray.destroy()
})
window.show()
```

The module contains no application-specific labels or lifecycle policy.

## Integrated native check

From this package directory on the Windows build host, build the local x64
adapter test prebuild and run the real primitives together. The explicit
`BARE_WIN_UI_TESTING` definition enables native seams used only by this check;
production builds leave it disabled and do not export those controls:

```console
bare-make generate --source . --build build --platform win32 --arch x64 --define BARE_WIN_UI_TESTING:BOOL=ON
bare-make build --build build
bare-make install --build build --prefix prebuilds
bare-build --base . --host win32-x64 --runtime ./runtime.js --out sample-build sample.js
.\\sample-build\\bare-win-ui\\App\\bare-win-ui.exe
```

The sample exits nonzero on a failed readiness/message exchange, native close
request cancellation and reuse of the same window, native menu selection,
taskbar recreation seam, partial notification-area construction failure, or
teardown. It separately holds document-script registration and WebView
readiness, destroys each view, then releases the pending native callback and
injects a late message to verify no callback leaks through. Its private binding
seams exercise native user-close,
`TaskbarCreated`, failure cleanup, and callback suppression without adding
application policy to the public API.

The runtime's no-window exit path can be checked independently:

```console
bare-build --base . --host win32-x64 --runtime ./runtime.js --out no-window-build sample-no-window-exit.js
.\no-window-build\bare-win-ui\App\bare-win-ui.exe
```

It must exit promptly with code 1 without creating native UI.

## License

Apache-2.0
