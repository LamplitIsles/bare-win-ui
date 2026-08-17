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

`WebView` becomes ready asynchronously. `navigateToString()` and `postMessage()`
are ordered after readiness; page messages are emitted as `message` events.
`openExternal()` accepts only `http:` and `https:` URLs and uses the system
handler. `destroy()` is idempotent and prevents later callbacks.

`NotificationArea` uses the default application icon and exposes mutable menu
items through stable string IDs:

```js
const tray = new NotificationArea({ tooltip: 'Example' })
tray.addItem('open', 'Open')
tray.addSeparator()
tray.addItem('quit', 'Quit', true)
tray.updateItem('open', 'Open window', true)
tray.on('select', (id) => {})
```

Destroying the notification area removes its icon. The icon is re-added when
Windows broadcasts the `TaskbarCreated` restart notification.

The module contains no application-specific labels or lifecycle policy.

## License

Apache-2.0
