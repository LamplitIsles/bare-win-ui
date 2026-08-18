const { once } = require('bare-events')

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

function messageWithTimeout(expected) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`Timed out waiting for WebView message '${expected}'`))
    }, 10000)

    once(webView, 'message').then(([message]) => {
      clearTimeout(timer)
      if (message !== expected) {
        reject(new Error(`Expected WebView message '${expected}', got '${message}'`))
      } else {
        resolve()
      }
    }, reject)
  })
}

function shutdown(code) {
  if (shutdownStarted) return
  shutdownStarted = true

  try {
    if (pendingWebView !== null) {
      pendingWebView.destroy().destroy()
      binding.webViewTestReleaseReady(pendingWebView._handle)
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

async function main() {
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

  const pageReady = messageWithTimeout('page-ready')
  await webView.navigateToString(`
    <!doctype html>
    <meta charset="utf-8">
    <title>bare-win-ui sample</title>
    <script>
      const bridge = window.chrome.webview
      bridge.addEventListener('message', (event) => {
        if (event.data === 'host-ready') bridge.postMessage('page-ack')
      })
      bridge.postMessage('page-ready')
    </script>
  `)
  await pageReady

  const pageAck = messageWithTimeout('page-ack')
  await webView.postMessage('host-ready')
  await pageAck

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
