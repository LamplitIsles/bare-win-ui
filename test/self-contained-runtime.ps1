param(
  [Parameter(Mandatory = $true)]
  [string]$PrebuildRoot,
  [Parameter(Mandatory = $true)]
  [string]$SampleAppDirectory,
  [Parameter(Mandatory = $true)]
  [string]$Node,
  [Parameter(Mandatory = $true)]
  [string]$Dumpbin,
  [string]$ManifestTool,
  [string]$StagingRoot
)

$ErrorActionPreference = 'Stop'
if (-not $ManifestTool) {
  $ManifestTool = Join-Path $PSScriptRoot '..\cmake\self-contained-runtime.js'
}
$manifestTool = [IO.Path]::GetFullPath($ManifestTool)
$PrebuildRoot = [IO.Path]::GetFullPath($PrebuildRoot)
$SampleAppDirectory = [IO.Path]::GetFullPath($SampleAppDirectory)
if (-not $StagingRoot) {
  $StagingRoot = Join-Path ([IO.Path]::GetTempPath()) "bare-win-ui-self-contained-$([Guid]::NewGuid())"
}
$StagingRoot = [IO.Path]::GetFullPath($StagingRoot)
New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null

function Copy-Payload($name) {
  $destination = Join-Path $StagingRoot $name
  New-Item -ItemType Directory -Path $destination -Force | Out-Null
  Copy-Item -Path (Join-Path $PrebuildRoot '*') -Destination $destination -Recurse -Force
  return $destination
}

function Validate-Payload($root) {
  & $Node $manifestTool validate $root
  if ($LASTEXITCODE -ne 0) { throw "Manifest validation failed for $root" }
}

function Expect-ManifestFailure($label, $root) {
  & $Node $manifestTool validate $root
  if ($LASTEXITCODE -eq 0) { throw "$label unexpectedly validated" }
  Write-Output "$label=PASS"
}

function Run-Process($executable, $workingDirectory, $userData) {
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = $executable
  $psi.WorkingDirectory = $workingDirectory
  $psi.UseShellExecute = $false
  if ($userData) { $psi.EnvironmentVariables['WEBVIEW2_USER_DATA_FOLDER'] = $userData }
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $psi
  [void]$process.Start()
  if (-not $process.WaitForExit(60000)) {
    $process.Kill()
    throw "Process timed out: $executable"
  }
  return $process.ExitCode
}

try {
  $complete = Copy-Payload 'complete'
  Validate-Payload $complete

  $manifestPath = Join-Path $complete 'self-contained-runtime.json'
  $before = [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath))
  & $Node $manifestTool generate $complete
  if ($LASTEXITCODE -ne 0) { throw 'Manifest regeneration failed' }
  $after = [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath))
  if ($before -ne $after) { throw 'Manifest generation was not deterministic' }

  $manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json
  if ($manifest.schemaVersion -ne 1 -or $manifest.architecture -ne 'x64') {
    throw 'Manifest schema or architecture is incorrect'
  }
  $paths = @($manifest.files | ForEach-Object { $_.path })
  if ($paths -contains 'self-contained-runtime.json' -or $paths -contains 'bare.exe' -or
      $paths -contains 'Microsoft.Web.WebView2.Core.dll') {
    throw 'Manifest includes an excluded file'
  }
  for ($index = 0; $index -lt $paths.Count; $index++) {
    $path = $paths[$index]
    $segments = $path.Split('/')
    if ($path.Contains('\') -or $segments.Count -eq 0 -or
        ($segments | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' })) {
      throw "Unsafe manifest path: $path"
    }
    if ($index -gt 0 -and [String]::CompareOrdinal($paths[$index - 1], $path) -ge 0) {
      throw 'Manifest paths are not ordinal-sorted and unique'
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path $complete ($path -replace '/', '\')))
    $rootPrefix = "$([IO.Path]::GetFullPath($complete).TrimEnd('\'))\"
    if (-not $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Manifest path escapes the payload root: $path"
    }
  }
  Write-Output 'schema-determinism-containment=PASS'

  $missing = Copy-Payload 'missing'
  Remove-Item (Join-Path $missing 'Microsoft.WindowsAppRuntime.dll')
  Expect-ManifestFailure 'missing-payload' $missing

  $altered = Copy-Payload 'altered'
  [IO.File]::AppendAllText((Join-Path $altered 'Microsoft.WindowsAppRuntime.pri'), 'altered')
  Expect-ManifestFailure 'altered-payload' $altered

  $duplicate = Copy-Payload 'duplicate'
  $duplicateManifestPath = Join-Path $duplicate 'self-contained-runtime.json'
  $duplicateManifest = Get-Content -Raw $duplicateManifestPath | ConvertFrom-Json
  $duplicateManifest.files = @($duplicateManifest.files) + @($duplicateManifest.files[0])
  $duplicateManifest | ConvertTo-Json -Depth 10 | Set-Content $duplicateManifestPath
  Expect-ManifestFailure 'duplicate-payload-entry' $duplicate

  $unmanifested = Copy-Payload 'unmanifested'
  Set-Content (Join-Path $unmanifested 'unmanifested-runtime.dll') 'unmanifested'
  Expect-ManifestFailure 'unmanifested-payload' $unmanifested

  $unsafe = Copy-Payload 'unsafe'
  $unsafeManifestPath = Join-Path $unsafe 'self-contained-runtime.json'
  $unsafeManifest = Get-Content -Raw $unsafeManifestPath | ConvertFrom-Json
  $unsafeManifest.files[0].path = '../escape.dll'
  $unsafeManifest | ConvertTo-Json -Depth 10 | Set-Content $unsafeManifestPath
  Expect-ManifestFailure 'unsafe-payload-path' $unsafe

  $hardLink = Copy-Payload 'hard-link'
  New-Item -ItemType HardLink -Path (Join-Path $hardLink 'linked-runtime.dll') -Target (Join-Path $hardLink 'Microsoft.WindowsAppRuntime.dll') | Out-Null
  Expect-ManifestFailure 'linked-payload' $hardLink

  $prebuild = Join-Path (Split-Path $PrebuildRoot) 'bare.exe'
  $dependencies = & $Dumpbin /DEPENDENTS $prebuild | Out-String
  if ($dependencies -notmatch '(?im)^\s*Microsoft\.WindowsAppRuntime\.dll\s*$') {
    throw 'Executable does not import Microsoft.WindowsAppRuntime.dll'
  }
  if ($dependencies -match '(?im)Microsoft\.WindowsAppRuntime\.Bootstrap\.dll') {
    throw 'Executable imports the Bootstrap DLL'
  }
  if (Test-Path (Join-Path $PrebuildRoot 'Microsoft.WindowsAppRuntime.Bootstrap.dll')) {
    throw 'x64 payload ships the Bootstrap DLL'
  }
  Write-Output 'imports-and-bootstrap-exclusion=PASS'

  $healthy = Join-Path $StagingRoot 'healthy-app'
  New-Item -ItemType Directory -Path $healthy -Force | Out-Null
  Copy-Item -Path (Join-Path $SampleAppDirectory '*') -Destination $healthy -Recurse -Force
  $healthyExe = (Get-ChildItem $healthy -Filter '*.exe' | Select-Object -First 1).FullName
  $healthyData = Join-Path $healthy 'WebView2'
  $healthyExit = Run-Process $healthyExe $healthy $healthyData
  if ($healthyExit -ne 0) { throw "Healthy sample exited $healthyExit" }
  Write-Output 'healthy-native-sample=PASS'

  $damaged = Join-Path $StagingRoot 'damaged-import'
  New-Item -ItemType Directory -Path $damaged -Force | Out-Null
  Copy-Item -Path (Join-Path $SampleAppDirectory '*') -Destination $damaged -Recurse -Force
  Remove-Item (Join-Path $damaged 'Microsoft.WindowsAppRuntime.dll')
  $damagedExe = (Get-ChildItem $damaged -Filter '*.exe' | Select-Object -First 1).FullName
  $started = $false
  try {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $damagedExe
    $psi.WorkingDirectory = $damaged
    $psi.UseShellExecute = $false
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    $started = $true
    if (-not $process.WaitForExit(30000)) {
      $process.Kill()
      throw 'Damaged payload process timed out'
    }
    if ($process.ExitCode -eq 0) { throw 'Damaged imported DLL unexpectedly allowed startup' }
  } catch {
    if ($started) { throw }
  }
  Write-Output 'damaged-import-nonzero=PASS'

  $alteredImport = Join-Path $StagingRoot 'altered-import'
  New-Item -ItemType Directory -Path $alteredImport -Force | Out-Null
  Copy-Item -Path (Join-Path $SampleAppDirectory '*') -Destination $alteredImport -Recurse -Force
  $runtimeDll = Join-Path $alteredImport 'Microsoft.WindowsAppRuntime.dll'
  $runtimeBytes = [IO.File]::ReadAllBytes($runtimeDll)
  $runtimeBytes[0] = $runtimeBytes[0] -bxor 0xff
  [IO.File]::WriteAllBytes($runtimeDll, $runtimeBytes)
  $alteredImportExe = (Get-ChildItem $alteredImport -Filter '*.exe' | Select-Object -First 1).FullName
  $started = $false
  try {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $alteredImportExe
    $psi.WorkingDirectory = $alteredImport
    $psi.UseShellExecute = $false
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    $started = $true
    if (-not $process.WaitForExit(30000)) {
      $process.Kill()
      throw 'Altered imported DLL process timed out'
    }
    if ($process.ExitCode -eq 0) { throw 'Altered imported DLL unexpectedly allowed startup' }
  } catch {
    if ($started) { throw }
  }
  Write-Output 'altered-import-nonzero=PASS'
} finally {
  Remove-Item $StagingRoot -Recurse -Force -ErrorAction SilentlyContinue
}
