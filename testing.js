const binding = require('./binding')

exports.requestWindowClose = (window) => {
  binding.windowTestCloseRequest(window._handle)
}

exports.simulateTaskbarCreated = (notificationArea) => {
  binding.notificationAreaTestTaskbarCreated(notificationArea._handle)
}

exports.selectNotificationItem = (notificationArea, id) => {
  binding.notificationAreaTestSelect(notificationArea._handle, id)
}
