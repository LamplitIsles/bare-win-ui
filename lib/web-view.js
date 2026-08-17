const WinUIElement = require('./element')
const binding = require('../binding')

module.exports = exports = class WinUIWebView extends WinUIElement {
  constructor() {
    super()

    this._ready = Promise.withResolvers()
    this._readyState = 'pending'
    this._destroyed = false

    this._handle = binding.webViewInit(this, this._onready, this._onmessage)
  }

  get width() {
    return binding.webViewWidth(this._handle)
  }

  set width(value) {
    binding.webViewWidth(this._handle, value)
  }

  get height() {
    return binding.webViewHeight(this._handle)
  }

  set height(value) {
    binding.webViewHeight(this._handle, value)
  }

  get source() {
    return binding.webViewSource(this._handle)
  }

  set source(value) {
    binding.webViewSource(this._handle, value)
  }

  async navigate(url) {
    await this._ready.promise

    if (!this._destroyed) binding.webViewNavigate(this._handle, url)

    return this
  }

  async navigateToString(html) {
    await this._ready.promise

    if (!this._destroyed) binding.webViewNavigateToString(this._handle, html)

    return this
  }

  async postMessage(message) {
    await this._ready.promise

    if (!this._destroyed) binding.webViewPostMessage(this._handle, message)

    return this
  }

  async openDevToolsWindow() {
    await this._ready.promise

    if (!this._destroyed) binding.webViewOpenDevToolsWindow(this._handle)

    return this
  }

  openExternal(url) {
    if (this._destroyed) return this

    binding.webViewOpenExternal(this._handle, url)
    return this
  }

  destroy() {
    if (this._destroyed) return this

    this._destroyed = true
    binding.webViewDestroy(this._handle)

    if (this._readyState === 'pending') {
      this._readyState = 'rejected'
      this._ready.reject(new Error('WebView was destroyed'))
    }

    return this
  }

  [Symbol.for('bare.inspect')]() {
    return {
      __proto__: { constructor: WinUIWebView },

      width: this.width,
      height: this.height,
      source: this.source
    }
  }

  _onready(err) {
    if (this._readyState !== 'pending') return

    if (err) {
      this._readyState = 'rejected'
      this._ready.reject(new Error(err))
    } else {
      this._readyState = 'ready'
      this._ready.resolve()
    }
  }

  _onmessage(message) {
    if (!this._destroyed) this.emit('message', message)
  }
}
