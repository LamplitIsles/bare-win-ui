const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')

const MANIFEST_FILE = 'self-contained-runtime.json'
const EXECUTABLE_FILE = 'bare.exe'
const WEBVIEW2_FILE = 'Microsoft.Web.WebView2.Core.dll'
const ARCHITECTURE = 'x64'

const EXCLUDED_PATHS = new Set([MANIFEST_FILE, EXECUTABLE_FILE, WEBVIEW2_FILE])
const FOREIGN_ARCHITECTURE_SEGMENTS = new Set([
  'arm',
  'arm64',
  'arm64ec',
  'ia32',
  'win-arm64',
  'win-arm64ec',
  'win-x86',
  'x86'
])

function ordinalCompare(left, right) {
  if (left < right) return -1
  if (left > right) return 1
  return 0
}

function fail(message) {
  throw new Error(message)
}

function resolveRoot(root) {
  if (typeof root !== 'string' || root.length === 0) {
    fail('Payload root must be a non-empty path')
  }

  const absolute = path.resolve(root)
  const stat = fs.lstatSync(absolute, { throwIfNoEntry: false })
  if (!stat) fail(`Payload root does not exist: ${root}`)
  if (stat.isSymbolicLink()) fail(`Payload root must not be a link: ${root}`)
  if (!stat.isDirectory()) fail(`Payload root must be a directory: ${root}`)

  return { absolute, real: fs.realpathSync(absolute) }
}

function assertContained(root, candidate, label) {
  const relative = path.relative(root, candidate)
  if (relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)) {
    fail(`${label} escapes the payload root`)
  }
}

function assertNoLink(file, stat, label) {
  if (stat.isSymbolicLink()) fail(`${label} must not be a symbolic link`)
  if (stat.nlink > 1) fail(`${label} must not be a hard link`)
}

function normalizeManifestPath(value) {
  if (typeof value !== 'string' || value.length === 0) {
    fail('Manifest paths must be non-empty strings')
  }
  if (value.includes('\\') || value.includes('\0')) {
    fail(`Manifest path is not portable: ${JSON.stringify(value)}`)
  }
  if (value.startsWith('/') || /^[A-Za-z]:/.test(value)) {
    fail(`Manifest path must be relative: ${JSON.stringify(value)}`)
  }

  const segments = value.split('/')
  if (segments.some((segment) => segment.length === 0 || segment === '.' || segment === '..')) {
    fail(`Manifest path contains an unsafe segment: ${JSON.stringify(value)}`)
  }

  for (const segment of segments) {
    if (FOREIGN_ARCHITECTURE_SEGMENTS.has(segment.toLowerCase())) {
      fail(`Manifest path is not x64-only: ${JSON.stringify(value)}`)
    }
  }

  if (path.posix.normalize(value) !== value) {
    fail(`Manifest path is not normalized: ${JSON.stringify(value)}`)
  }

  return value
}

function resolveManifestEntry(root, manifestPath) {
  const candidate = path.resolve(root.absolute, ...manifestPath.split('/'))
  assertContained(root.absolute, candidate, `Manifest path ${JSON.stringify(manifestPath)}`)
  return candidate
}

function walkPayload(root) {
  const files = []

  function visit(directory, relativeDirectory) {
    const entries = fs
      .readdirSync(directory, { withFileTypes: true })
      .sort((left, right) => ordinalCompare(left.name, right.name))

    for (const entry of entries) {
      const relativePath = relativeDirectory ? `${relativeDirectory}/${entry.name}` : entry.name
      const normalizedPath = normalizeManifestPath(relativePath)
      const absolutePath = path.join(directory, entry.name)
      const stat = fs.lstatSync(absolutePath)

      if (entry.isSymbolicLink()) {
        fail(`Payload path must not be a symbolic link: ${normalizedPath}`)
      }

      if (entry.isDirectory()) {
        visit(absolutePath, normalizedPath)
        continue
      }

      if (!entry.isFile()) {
        fail(`Payload path must be a regular file or directory: ${normalizedPath}`)
      }

      assertNoLink(absolutePath, stat, `Payload path ${normalizedPath}`)
      if (!EXCLUDED_PATHS.has(normalizedPath)) {
        files.push({ path: normalizedPath, absolutePath })
      }
    }
  }

  visit(root.absolute, '')
  return files.sort((left, right) => ordinalCompare(left.path, right.path))
}

function fileDigest(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')
}

function makeManifest(files) {
  return {
    schemaVersion: 1,
    architecture: ARCHITECTURE,
    files: files.map(({ path: filePath, absolutePath }) => {
      const stat = fs.statSync(absolutePath)
      return {
        path: filePath,
        size: stat.size,
        sha256: fileDigest(absolutePath)
      }
    })
  }
}

function serializeManifest(manifest) {
  return `${JSON.stringify(manifest, null, 2)}\n`
}

function exactKeys(value, expected, label) {
  const keys = Object.keys(value)
  if (keys.length !== expected.length || keys.some((key, index) => key !== expected[index])) {
    fail(`${label} has an invalid shape`)
  }
}

function validateManifestShape(manifest) {
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    fail('Manifest must be an object')
  }
  exactKeys(manifest, ['schemaVersion', 'architecture', 'files'], 'Manifest')

  if (manifest.schemaVersion !== 1) fail('Manifest schemaVersion must be 1')
  if (manifest.architecture !== ARCHITECTURE) {
    fail(`Manifest architecture must be ${ARCHITECTURE}`)
  }
  if (!Array.isArray(manifest.files)) fail('Manifest files must be an array')

  let previousPath = null
  for (const file of manifest.files) {
    if (!file || typeof file !== 'object' || Array.isArray(file)) {
      fail('Manifest file entries must be objects')
    }
    exactKeys(file, ['path', 'size', 'sha256'], 'Manifest file entry')

    const filePath = normalizeManifestPath(file.path)
    if (EXCLUDED_PATHS.has(filePath)) {
      fail(`Manifest must not list excluded path: ${filePath}`)
    }
    if (previousPath !== null && ordinalCompare(previousPath, filePath) >= 0) {
      fail('Manifest paths must be unique and ordinal-sorted')
    }
    previousPath = filePath

    if (!Number.isSafeInteger(file.size) || file.size < 0) {
      fail(`Manifest size is invalid for ${filePath}`)
    }
    if (!/^[0-9a-f]{64}$/.test(file.sha256)) {
      fail(`Manifest SHA-256 is invalid for ${filePath}`)
    }
  }
}

function readManifest(manifestPath) {
  let source
  try {
    source = fs.readFileSync(manifestPath, 'utf8')
  } catch (error) {
    fail(`Unable to read manifest ${manifestPath}: ${error.message}`)
  }

  let manifest
  try {
    manifest = JSON.parse(source)
  } catch (error) {
    fail(`Unable to parse manifest ${manifestPath}: ${error.message}`)
  }

  validateManifestShape(manifest)
  if (source !== serializeManifest(manifest)) {
    fail('Manifest is not in the deterministic canonical form')
  }
  return manifest
}

function generateManifest(root, options = {}) {
  const resolvedRoot = resolveRoot(root)
  const manifestPath = path.resolve(
    options.manifestPath || path.join(resolvedRoot.absolute, MANIFEST_FILE)
  )
  if (manifestPath !== path.join(resolvedRoot.absolute, MANIFEST_FILE)) {
    fail('Manifest must be beside the payload root')
  }

  const files = walkPayload(resolvedRoot)
  const manifest = makeManifest(files)
  fs.writeFileSync(manifestPath, serializeManifest(manifest))
  validateManifest(manifestPath, { root: resolvedRoot.absolute })
  return manifest
}

function validateManifest(manifestPath, options = {}) {
  if (typeof manifestPath !== 'string' || manifestPath.length === 0) {
    fail('Manifest path must be a non-empty path')
  }

  const manifestAbsolute = path.resolve(manifestPath)
  const root = resolveRoot(options.root || path.dirname(manifestAbsolute))
  const expectedManifestPath = path.join(root.absolute, MANIFEST_FILE)
  if (manifestAbsolute !== expectedManifestPath) {
    fail('Manifest must be beside the payload root')
  }

  const manifestStat = fs.lstatSync(manifestAbsolute, { throwIfNoEntry: false })
  if (!manifestStat) fail(`Manifest does not exist: ${manifestAbsolute}`)
  assertNoLink(manifestAbsolute, manifestStat, 'Manifest')
  if (!manifestStat.isFile()) fail('Manifest must be a regular file')

  const manifest = readManifest(manifestAbsolute)
  const payloadFiles = walkPayload(root)
  const payloadByPath = new Map(payloadFiles.map((file) => [file.path, file]))

  for (const entry of manifest.files) {
    const payload = payloadByPath.get(entry.path)
    if (!payload) fail(`Manifest payload is missing: ${entry.path}`)

    const payloadPath = resolveManifestEntry(root, entry.path)
    const stat = fs.lstatSync(payloadPath)
    assertNoLink(payloadPath, stat, `Manifest payload ${entry.path}`)
    const realPath = fs.realpathSync(payloadPath)
    assertContained(root.real, realPath, `Manifest payload ${entry.path}`)
    if (stat.size !== entry.size) fail(`Manifest size mismatch: ${entry.path}`)
    if (fileDigest(payloadPath) !== entry.sha256) {
      fail(`Manifest SHA-256 mismatch: ${entry.path}`)
    }
  }

  if (payloadByPath.size !== manifest.files.length) {
    const declared = new Set(manifest.files.map((entry) => entry.path))
    const unmanifested = payloadFiles
      .filter((file) => !declared.has(file.path))
      .map((file) => file.path)
    fail(`Manifest has unmanifested payload: ${unmanifested.join(', ')}`)
  }

  return manifest
}

function cli() {
  const [, , command, root, manifestArgument] = process.argv
  if (!command || !root || !['generate', 'validate'].includes(command)) {
    throw new Error(
      `Usage: ${path.basename(process.argv[1])} <generate|validate> <payload-root> [manifest]`
    )
  }

  if (command === 'generate') {
    generateManifest(root, manifestArgument ? { manifestPath: manifestArgument } : {})
    return
  }

  validateManifest(manifestArgument || path.join(root, MANIFEST_FILE), { root })
}

if (require.main === module) {
  try {
    cli()
  } catch (error) {
    console.error(error.message)
    process.exitCode = 1
  }
}

module.exports = {
  ARCHITECTURE,
  MANIFEST_FILE,
  generateManifest,
  serializeManifest,
  validateManifest
}
