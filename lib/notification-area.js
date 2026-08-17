const EventEmitter = require('bare-events')
const binding = require('../binding')

module.exports = exports = class WinUINotificationArea extends EventEmitter {
  constructor(opts = {}) {
    super()

    const tooltip = typeof opts === 'string' ? opts : opts.tooltip || 'Application'

    this._destroyed = false
    this._handle = binding.notificationAreaInit(this, tooltip, this._onselect)
  }

  addItem(id, title, enabled = true) {
    binding.notificationAreaAddItem(this._handle, id, title, enabled)
    return this
  }

  addSeparator() {
    binding.notificationAreaAddSeparator(this._handle)
    return this
  }

  updateItem(id, title, enabled = true) {
    binding.notificationAreaUpdateItem(this._handle, id, title, enabled)
    return this
  }

  removeItem(id) {
    binding.notificationAreaRemoveItem(this._handle, id)
    return this
  }

  clear() {
    binding.notificationAreaClear(this._handle)
    return this
  }

  destroy() {
    if (this._destroyed) return this

    this._destroyed = true
    binding.notificationAreaDestroy(this._handle)
    return this
  }

  _onselect(id) {
    if (!this._destroyed) this.emit('select', id)
  }

  [Symbol.for('bare.inspect')]() {
    return {
      __proto__: { constructor: WinUINotificationArea }
    }
  }
}
