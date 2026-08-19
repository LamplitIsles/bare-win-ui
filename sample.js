const binding = require('./binding')
const { Window, WebView, NotificationArea } = require('./')

let window = null
let webView = null
let notificationArea = null
let pendingWebView = null
let closeEvents = 0
let shutdownStarted = false

function check(condition, message) {
  if (!condition) throw new Error(message)
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds))
}

async function waitFor(condition, message) {
  const deadline = Date.now() + 10000
  while (!condition()) {
    if (Date.now() >= deadline) throw new Error(message)
    await delay(10)
  }
}

async function expectRejected(promise, message) {
  try {
    await promise
  } catch (error) {
    check(error && error.message === message, `Expected '${message}'`)
    return
  }
  throw new Error(`Expected rejection '${message}'`)
}

function messageWithTimeout(view, expected) {
  return new Promise((resolve, reject) => {
    const onMessage = (message) => {
      if (message !== expected) return
      clearTimeout(timer)
      view.off('message', onMessage)
      resolve()
    }
    const timer = setTimeout(() => {
      view.off('message', onMessage)
      reject(new Error(`Timed out waiting for WebView message '${expected}'`))
    }, 10000)

    view.on('message', onMessage)
  })
}

function shutdown(code) {
  if (shutdownStarted) return
  shutdownStarted = true

  try {
    if (pendingWebView !== null) {
      pendingWebView.destroy().destroy()
      binding.webViewTestReleaseReady(pendingWebView._handle)
      binding.webViewTestReleaseScript(pendingWebView._handle)
    }
    if (webView !== null) webView.destroy().destroy()
    if (notificationArea !== null) notificationArea.destroy().destroy()
    if (window !== null) window.close().close()
  } catch {
    code = 1
  }

  setTimeout(() => {
    if (closeEvents !== 1) code = 1
    if (binding.notificationAreaTestLiveResources() !== 0) code = 1
    Bare.exit(code)
  }, 0)
}

function verifyPartialConstructionFailure() {
  const baseline = binding.notificationAreaTestLiveResources()
  binding.notificationAreaTestFailInit()

  let failed = false
  try {
    new NotificationArea({ tooltip: 'failed construction' })
  } catch {
    failed = true
  }

  check(failed, 'native partial construction did not fail')
  check(
    binding.notificationAreaTestLiveResources() === baseline,
    'native resources leaked after partial construction'
  )
}

async function verifyPendingBridge() {
  binding.webViewTestHoldScript()
  pendingWebView = new WebView()

  let navigationSettled = false
  let messageSettled = false
  const pageReady = messageWithTimeout(pendingWebView, 'bridge-page-ready')
  const pageAck = messageWithTimeout(pendingWebView, 'bridge-page-ack')
  const navigation = pendingWebView
    .navigateToString(
      `
      <!doctype html>
      <meta charset="utf-8">
      <script>
        window.addEventListener('bare-native-message', (event) => {
          if (event.data === 'bridge-host-ready') {
            window.bareNative.postMessage('bridge-page-ack')
          }
        })
        setTimeout(() => window.bareNative.postMessage('bridge-page-ready'), 0)
      </script>
    `
    )
    .then(() => {
      navigationSettled = true
    })
  const message = pendingWebView.postMessage('bridge-host-ready').then(() => {
    messageSettled = true
  })

  await waitFor(
    () => binding.webViewTestScriptPending(pendingWebView._handle),
    'WebView bridge registration did not become pending'
  )
  await delay(0)
  check(!navigationSettled, 'navigation ran before bridge registration')
  check(!messageSettled, 'host message ran before bridge registration')

  binding.webViewTestReleaseScript(pendingWebView._handle)
  await Promise.all([navigation, message])
  await pageReady
  await pendingWebView.postMessage('bridge-host-ready')
  await pageAck

  pendingWebView.destroy().destroy()
  pendingWebView = null
}

async function verifyBridgeRegistrationFailure() {
  binding.webViewTestFailScript()
  pendingWebView = new WebView()

  let messages = 0
  pendingWebView.on('message', () => messages++)
  const pendingOperation = pendingWebView.navigateToString('<p>bridge failed</p>')

  await expectRejected(pendingOperation, 'WebView bridge initialization failed')
  check(messages === 0, 'WebView delivered a message after bridge failure')

  pendingWebView.destroy().destroy()
  pendingWebView = null
}

async function verifyPendingWebViewTeardown() {
  binding.webViewTestHoldReady()
  pendingWebView = new WebView()

  let messages = 0
  pendingWebView.on('message', () => messages++)
  const pendingOperation = pendingWebView.navigateToString('<p>never ready</p>')

  await waitFor(
    () => binding.webViewTestReadyPending(pendingWebView._handle),
    'WebView readiness did not become pending'
  )

  pendingWebView.destroy().destroy()
  await expectRejected(pendingOperation, 'WebView was destroyed')
  binding.webViewTestMessage(pendingWebView._handle, 'late callback')
  binding.webViewTestReleaseReady(pendingWebView._handle)
  await delay(0)

  check(messages === 0, 'WebView delivered a callback after teardown')
  pendingWebView = null
}

async function verifyNonStringMessage() {
  let messages = 0
  const onMessage = () => messages++
  webView.on('message', onMessage)

  binding.webViewTestNonStringMessage(webView._handle)
  await delay(100)

  webView.off('message', onMessage)
  check(messages === 0, 'WebView delivered a non-string message')
}

async function main() {
  await verifyPendingBridge()
  await verifyBridgeRegistrationFailure()
  await verifyPendingWebViewTeardown()

  window = new Window()
  webView = new WebView()
  notificationArea = new NotificationArea({ tooltip: 'bare-win-ui sample' })

  window.title = 'bare-win-ui sample'
  window.content = webView
  window.on('close-request', (event) => {
    event.preventDefault()
    window.hide()
  })
  window.on('close', () => closeEvents++)

  notificationArea
    .addItem('open', 'Open')
    .addSeparator()
    .addItem('quit', 'Quit', false)
    .updateItem('quit', 'Quit', true)

  let selected = null
  notificationArea.on('select', (id) => {
    selected = id
    if (id === 'open') window.show()
    if (id === 'quit') shutdown(0)
  })

  window.show()

  const pageReady = messageWithTimeout(webView, 'page-ready')
  await webView.navigateToString(`
    <!doctype html>
    <meta charset="utf-8">
    <title>bare-win-ui sample</title>
    <script>
      window.addEventListener('bare-native-message', (event) => {
        if (event.data === 'host-ready') window.bareNative.postMessage('page-ack')
      })
      window.bareNative.postMessage('page-ready')
    </script>
  `)
  await pageReady

  const pageAck = messageWithTimeout(webView, 'page-ack')
  await webView.postMessage('host-ready')
  await pageAck
  await verifyNonStringMessage()

  binding.windowTestCloseRequest(window._handle)
  check(closeEvents === 0, 'native close request destroyed the window')
  window.show()

  binding.windowTestCloseRequest(window._handle)
  check(closeEvents === 0, 'same window could not be shown after close request')

  binding.notificationAreaTestTaskbarCreated(notificationArea._handle)
  binding.notificationAreaTestSelect(notificationArea._handle, 'open')
  check(selected === 'open', 'notification menu selection was not delivered')

  await verifyPartialConstructionFailure()
  binding.notificationAreaTestSelect(notificationArea._handle, 'quit')
}

main().catch(() => shutdown(1))
