const assert = require('node:assert/strict')
const crypto = require('node:crypto')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')

const {
  generateManifest,
  serializeManifest,
  validateManifest
} = require('../cmake/self-contained-runtime')
const { generate: generateRuntimeManifest } = require('../cmake/self-contained-runtime-manifest')

const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'bare-win-ui-runtime-'))
const sourceRoot = path.join(temporaryRoot, 'source')

function writePayload(relativePath, content) {
  const file = path.join(sourceRoot, ...relativePath.split('/'))
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, content)
}

function stage(name) {
  const stagedRoot = path.join(temporaryRoot, name)
  fs.cpSync(sourceRoot, stagedRoot, { recursive: true })
  return stagedRoot
}

function expectInvalid(stagedRoot, message) {
  assert.throws(
    () => validateManifest(path.join(stagedRoot, 'self-contained-runtime.json')),
    new RegExp(message)
  )
}

try {
  fs.mkdirSync(sourceRoot, { recursive: true })

  const payloads = {
    'Microsoft.WindowsAppRuntime.dll': Buffer.from('runtime-dll'),
    'Microsoft.WindowsAppRuntime.pri': Buffer.from('runtime-pri'),
    'en-us/Microsoft.ui.xaml.dll.mui': Buffer.from('locale-resource'),
    'Microsoft.UI.Xaml/Assets/map.html': Buffer.from('<map />'),
    'metadata/Microsoft.UI.Xaml.winmd': Buffer.from('winmd-metadata')
  }

  for (const [relativePath, content] of Object.entries(payloads)) {
    writePayload(relativePath, content)
  }
  writePayload('Microsoft.Web.WebView2.Core.dll', 'webview-is-separate')
  writePayload('bare.exe', 'executable-is-separate')

  const manifest = generateManifest(sourceRoot)
  const expected = Object.entries(payloads)
    .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
    .map(([relativePath, content]) => ({
      path: relativePath,
      size: content.length,
      sha256: crypto.createHash('sha256').update(content).digest('hex')
    }))

  assert.deepEqual(manifest, {
    schemaVersion: 1,
    architecture: 'x64',
    files: expected
  })
  assert.equal(
    fs.readFileSync(path.join(sourceRoot, 'self-contained-runtime.json'), 'utf8'),
    serializeManifest(manifest)
  )
  assert.deepEqual(validateManifest(path.join(sourceRoot, 'self-contained-runtime.json')), manifest)

  const deterministic = fs.readFileSync(
    path.join(sourceRoot, 'self-contained-runtime.json'),
    'utf8'
  )
  generateManifest(sourceRoot)
  assert.equal(
    fs.readFileSync(path.join(sourceRoot, 'self-contained-runtime.json'), 'utf8'),
    deterministic
  )

  const missing = stage('missing')
  fs.rmSync(path.join(missing, 'Microsoft.WindowsAppRuntime.dll'))
  expectInvalid(missing, 'missing')

  const altered = stage('altered')
  fs.appendFileSync(path.join(altered, 'Microsoft.WindowsAppRuntime.dll'), 'altered')
  expectInvalid(altered, 'mismatch')

  const duplicate = stage('duplicate')
  const duplicateManifest = JSON.parse(
    fs.readFileSync(path.join(duplicate, 'self-contained-runtime.json'), 'utf8')
  )
  duplicateManifest.files.push(duplicateManifest.files[0])
  fs.writeFileSync(
    path.join(duplicate, 'self-contained-runtime.json'),
    serializeManifest(duplicateManifest)
  )
  expectInvalid(duplicate, 'unique')

  const unmanifested = stage('unmanifested')
  fs.writeFileSync(path.join(unmanifested, 'new-runtime.dll'), 'unmanifested')
  expectInvalid(unmanifested, 'unmanifested')

  const unsafe = stage('unsafe')
  const unsafeManifest = JSON.parse(
    fs.readFileSync(path.join(unsafe, 'self-contained-runtime.json'), 'utf8')
  )
  unsafeManifest.files[0].path = '../escape.dll'
  unsafeManifest.files.sort((left, right) =>
    left.path < right.path ? -1 : left.path > right.path ? 1 : 0
  )
  fs.writeFileSync(
    path.join(unsafe, 'self-contained-runtime.json'),
    serializeManifest(unsafeManifest)
  )
  expectInvalid(unsafe, 'unsafe segment')

  const wrongArchitecture = stage('wrong-architecture')
  const wrongArchitectureManifest = JSON.parse(
    fs.readFileSync(path.join(wrongArchitecture, 'self-contained-runtime.json'), 'utf8')
  )
  wrongArchitectureManifest.architecture = 'arm64'
  fs.writeFileSync(
    path.join(wrongArchitecture, 'self-contained-runtime.json'),
    serializeManifest(wrongArchitectureManifest)
  )
  expectInvalid(wrongArchitecture, 'architecture')

  const hardLink = stage('hard-link')
  fs.linkSync(
    path.join(hardLink, 'Microsoft.WindowsAppRuntime.dll'),
    path.join(hardLink, 'linked-runtime.dll')
  )
  expectInvalid(hardLink, 'hard link')

  const registrationRoot = path.join(temporaryRoot, 'registration')
  fs.mkdirSync(registrationRoot, { recursive: true })
  const registrationBase = path.join(registrationRoot, 'runtime.manifest')
  const registrationFragment = path.join(registrationRoot, 'package.appxfragment')
  const registrationOutput = path.join(registrationRoot, 'generated.manifest')
  fs.writeFileSync(
    registrationBase,
    '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"></assembly>\n'
  )
  fs.writeFileSync(
    registrationFragment,
    `<Fragment xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Extensions>
    <Extension Category="windows.activatableClass.inProcessServer">
      <InProcessServer>
        <Path>Microsoft.WindowsAppRuntime.dll</Path>
        <ActivatableClass ActivatableClassId="Microsoft.Windows.Foundation.Test" ThreadingModel="both" />
      </InProcessServer>
    </Extension>
  </Extensions>
</Fragment>
`
  )
  generateRuntimeManifest(registrationBase, registrationOutput, '0.2.1', [registrationFragment])
  const registrationManifest = fs.readFileSync(registrationOutput, 'utf8')
  assert.match(registrationManifest, /xmlns:asmv3="urn:schemas-microsoft-com:asm.v3"/)
  assert.match(registrationManifest, /<asmv3:file name="Microsoft.WindowsAppRuntime.dll">/)
  assert.match(
    registrationManifest,
    /<winrtv1:activatableClass name="Microsoft.Windows.Foundation.Test" threadingModel="both" \/>/
  )
  const registrationBytes = registrationManifest
  generateRuntimeManifest(registrationBase, registrationOutput, '0.2.1', [registrationFragment])
  assert.equal(fs.readFileSync(registrationOutput, 'utf8'), registrationBytes)

  const foreignArchitecture = stage('foreign-architecture')
  const foreignPath = path.join(foreignArchitecture, 'win-arm64', 'runtime.dll')
  fs.mkdirSync(path.dirname(foreignPath), { recursive: true })
  fs.writeFileSync(foreignPath, 'foreign-architecture')
  expectInvalid(foreignArchitecture, 'x64-only')

  console.log('self-contained-runtime manifest tests: PASS')
} finally {
  fs.rmSync(temporaryRoot, { recursive: true, force: true })
}
