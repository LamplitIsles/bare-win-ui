const fs = require('node:fs')
const path = require('node:path')

function ordinalCompare(left, right) {
  if (left < right) return -1
  if (left > right) return 1
  return 0
}

function escapeXml(value) {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;')
}

function attribute(tag, name) {
  const match = tag.match(new RegExp(`${name}="([^"]*)"`))
  if (!match) throw new Error(`Missing ${name} in ${tag}`)
  return match[1]
}

function readRegistrations(fragment) {
  const xml = fs.readFileSync(fragment, 'utf8')
  const registrations = []
  const servers = xml.matchAll(/<InProcessServer\b[^>]*>([\s\S]*?)<\/InProcessServer>/g)

  for (const server of servers) {
    const body = server[1]
    const pathMatch = body.match(/<Path>\s*([^<]+?)\s*<\/Path>/)
    if (!pathMatch) throw new Error(`Missing InProcessServer path in ${fragment}`)

    const file = pathMatch[1]
    const classes = []
    for (const classMatch of body.matchAll(/<ActivatableClass\b[^>]*\/>/g)) {
      const tag = classMatch[0]
      classes.push({
        name: attribute(tag, 'ActivatableClassId'),
        threadingModel: attribute(tag, 'ThreadingModel')
      })
    }

    if (classes.length === 0) throw new Error(`Missing classes for ${file} in ${fragment}`)
    registrations.push({ file, classes })
  }

  if (registrations.length === 0) throw new Error(`No InProcessServer registrations in ${fragment}`)
  return registrations
}

function makeSxSRegistrations(fragments) {
  const files = new Map()

  for (const fragment of fragments) {
    for (const registration of readRegistrations(fragment)) {
      let classes = files.get(registration.file)
      if (!classes) {
        classes = new Map()
        files.set(registration.file, classes)
      }

      for (const item of registration.classes) {
        const previous = classes.get(item.name)
        if (previous && previous !== item.threadingModel) {
          throw new Error(`Conflicting registration for ${item.name}`)
        }
        classes.set(item.name, item.threadingModel)
      }
    }
  }

  return [...files.entries()]
    .sort(([left], [right]) => ordinalCompare(left, right))
    .map(([file, classes]) => {
      const body = [...classes.entries()]
        .sort(([left], [right]) => ordinalCompare(left, right))
        .map(
          ([name, threadingModel]) =>
            `      <winrtv1:activatableClass name="${escapeXml(name)}" threadingModel="${escapeXml(threadingModel)}" />`
        )
        .join('\n')
      return `    <asmv3:file name="${escapeXml(file)}">\n${body}\n    </asmv3:file>`
    })
    .join('\n')
}

function generate(baseManifest, outputManifest, version, fragments) {
  if (!version || fragments.length === 0) {
    throw new Error(
      `Usage: ${path.basename(process.argv[1])} generate <base> <output> <version> <fragment...>`
    )
  }

  let manifest = fs.readFileSync(baseManifest, 'utf8').replaceAll('${version}', version)
  const assemblyStart = '<assembly xmlns="urn:schemas-microsoft-com:asm.v1"'
  if (!manifest.includes('xmlns:asmv3=')) {
    manifest = manifest.replace(
      assemblyStart,
      `${assemblyStart} xmlns:asmv3="urn:schemas-microsoft-com:asm.v3" xmlns:winrtv1="urn:schemas-microsoft-com:winrt.v1"`
    )
  }
  const registrations = makeSxSRegistrations(fragments)
  const closingTag = '</assembly>'
  const closingTagIndex = manifest.lastIndexOf(closingTag)
  if (closingTagIndex === -1) throw new Error(`Missing ${closingTag} in ${baseManifest}`)

  manifest = `${manifest.slice(0, closingTagIndex)}${registrations}\n${manifest.slice(closingTagIndex)}`
  fs.writeFileSync(outputManifest, manifest)
}

function cli() {
  const [, , command, baseManifest, outputManifest, version, ...fragments] = process.argv
  if (command !== 'generate') throw new Error('Only the generate command is supported')
  generate(baseManifest, outputManifest, version, fragments)
}

if (require.main === module) {
  try {
    cli()
  } catch (error) {
    console.error(error.message)
    process.exitCode = 1
  }
}

module.exports = { generate, makeSxSRegistrations }
