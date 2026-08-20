const { type } = require('bare-build/constants')

const { EXECUTABLE } = type

require.asset = require('require-asset')

const prebuilds = {}

exports.prebuilds = prebuilds

prebuilds['win32-x64'] = () => {
  return {
    type: EXECUTABLE,
    path: require.asset('./prebuilds/win32-x64/bare.exe', __filename),
    dependencies: [
      require.asset('./prebuilds/win32-x64/bare/Microsoft.Web.WebView2.Core.dll', __filename)
    ]
  }
}

prebuilds['win32-arm64'] = () => {
  return {
    type: EXECUTABLE,
    path: require.asset('./prebuilds/win32-arm64/bare.exe', __filename),
    dependencies: [
      require.asset('./prebuilds/win32-arm64/bare/Microsoft.Web.WebView2.Core.dll', __filename),
      require.asset(
        './prebuilds/win32-arm64/bare/Microsoft.WindowsAppRuntime.Bootstrap.dll',
        __filename
      )
    ]
  }
}
