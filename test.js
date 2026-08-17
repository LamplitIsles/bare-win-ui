const test = require('brittle')

const bindingPath = require.resolve('./binding')
const state = {
  window: null,
  webView: null,
  notificationArea: null,
  calls: []
}

const binding = {
  windowInit(ctx, onclosing, onclose, onresize) {
    state.window = { ctx, onclosing, onclose, onresize }
    return state.window
  },
  windowTitle() {},
  windowContent() {},
  windowActivate() {},
  windowHide() {},
  windowClose() {},
  windowResize() {},
  windowResizeClient() {},

  webViewInit(ctx, onready, onmessage) {
    state.webView = { ctx, onready, onmessage }
    return state.webView
  },
  webViewWidth() {},
  webViewHeight() {},
  webViewSource() {},
  webViewNavigate(handle, url) {
    state.calls.push(['navigate', url])
  },
  webViewNavigateToString(handle, html) {
    state.calls.push(['navigateToString', html])
  },
  webViewPostMessage(handle, message) {
    state.calls.push(['postMessage', message])
  },
  webViewOpenDevToolsWindow() {},
  webViewOpenExternal() {},
  webViewDestroy() {
    state.calls.push(['destroyWebView'])
  },

  notificationAreaInit(ctx, tooltip, onselect) {
    state.notificationArea = { ctx, tooltip, onselect }
    return state.notificationArea
  },
  notificationAreaAddItem() {
    state.calls.push(['addItem'])
  },
  notificationAreaAddSeparator() {
    state.calls.push(['addSeparator'])
  },
  notificationAreaUpdateItem() {
    state.calls.push(['updateItem'])
  },
  notificationAreaRemoveItem() {
    state.calls.push(['removeItem'])
  },
  notificationAreaClear() {
    state.calls.push(['clear'])
  },
  notificationAreaDestroy() {
    state.calls.push(['destroyNotificationArea'])
  }
}

require.cache[bindingPath] = {
  id: bindingPath,
  filename: bindingPath,
  loaded: true,
  exports: binding
}

const Window = require('./lib/window')
const WebView = require('./lib/web-view')
const NotificationArea = require('./lib/notification-area')

test('web view preserves operation order after readiness', async function (t) {
  state.calls = []
  const webView = new WebView()

  const first = webView.navigateToString('<p>first</p>')
  const second = webView.postMessage('second')
  const third = webView.navigate('https://example.test/third')

  t.is(state.calls.length, 0)

  state.webView.onready.call(state.webView.ctx, null)
  await Promise.all([first, second, third])

  t.alike(state.calls, [
    ['navigateToString', '<p>first</p>'],
    ['postMessage', 'second'],
    ['navigate', 'https://example.test/third']
  ])
})

test('web view destroy rejects pending work and suppresses late callbacks', async function (t) {
  state.calls = []
  const webView = new WebView()
  let messages = 0
  webView.on('message', () => messages++)

  const pending = webView.postMessage('never')
  webView.destroy().destroy()
  state.webView.onready.call(state.webView.ctx, null)
  state.webView.onmessage.call(state.webView.ctx, 'late')

  await t.exception(pending, /WebView was destroyed/)
  t.is(messages, 0)
  t.alike(state.calls, [['destroyWebView']])
})

test('notification area teardown is terminal for mutators and callbacks', function (t) {
  state.calls = []
  const tray = new NotificationArea({ tooltip: 'Test' })
  let selections = 0
  tray.on('select', () => selections++)

  tray.addItem('open', 'Open').addSeparator()
  tray.destroy().destroy()
  tray.addItem('late', 'Late').addSeparator()
  tray.updateItem('open', 'Still late')
  tray.removeItem('open').clear()
  state.notificationArea.onselect.call(state.notificationArea.ctx, 'late')

  t.alike(state.calls, [['addItem'], ['addSeparator'], ['destroyNotificationArea']])
  t.is(selections, 0)
})

test('window lifecycle callbacks carry close-request and resize events', function (t) {
  const window = new Window()
  let closeRequests = 0
  let closes = 0
  let resize = null

  window.on('close-request', (event) => {
    closeRequests++
    event.preventDefault()
  })
  window.on('close', () => closes++)
  window.on('resize', (size) => {
    resize = size
  })

  t.ok(state.window.onclosing.call(state.window.ctx))
  state.window.onresize.call(state.window.ctx, 640, 480)
  state.window.onclose.call(state.window.ctx)

  t.is(closeRequests, 1)
  t.is(closes, 1)
  t.alike(resize, { width: 640, height: 480 })
})
