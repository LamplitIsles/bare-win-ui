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
$ownedStagingChild = $null

function Get-FullPath([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path)) {
    throw 'Path must not be empty'
  }

  return [IO.Path]::GetFullPath($Path)
}

function Test-ReparsePoint($Item) {
  return (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Assert-SafeDirectoryPath([string]$Path, [string]$Label) {
  $fullPath = Get-FullPath $Path
  $root = [IO.Path]::GetPathRoot($fullPath)
  if ([string]::IsNullOrEmpty($root)) {
    throw "$Label has no filesystem root: $Path"
  }

  $current = $root
  $tail = $fullPath.Substring($root.Length)
  foreach ($segment in ($tail -split '[\\/]')) {
    if ([string]::IsNullOrEmpty($segment)) { continue }

    $current = [IO.Path]::Combine($current, $segment)
    $item = Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { continue }
    if (Test-ReparsePoint $item) {
      throw "$Label contains a reparse point: $current"
    }
    if (-not $item.PSIsContainer) {
      throw "$Label contains a file in its directory path: $current"
    }
  }

  return $fullPath
}

function Assert-SafeDirectoryTree([string]$Path, [string]$Label) {
  $fullPath = Assert-SafeDirectoryPath $Path $Label
  $root = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
  if (-not $root.PSIsContainer) { throw "$Label is not a directory: $fullPath" }
  if (Test-ReparsePoint $root) { throw "$Label is a reparse point: $fullPath" }

  $pending = New-Object 'System.Collections.Generic.Queue[string]'
  $pending.Enqueue($fullPath)
  while ($pending.Count -gt 0) {
    $directory = $pending.Dequeue()
    $entries = @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)
    foreach ($entry in $entries) {
      if (Test-ReparsePoint $entry) {
        throw "$Label contains a reparse point: $($entry.FullName)"
      }
      if ($entry.PSIsContainer) {
        $pending.Enqueue($entry.FullName)
      }
    }
  }

  return $fullPath
}

function Test-ContainedPath([string]$Parent, [string]$Candidate) {
  $parentFull = Get-FullPath $Parent
  $candidateFull = Get-FullPath $Candidate
  $prefix = ($parentFull -replace '[\\/]+$', '') + [IO.Path]::DirectorySeparatorChar
  return $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-ContainedChild([string]$Parent, [string]$Child, [string]$Label) {
  if (-not (Test-ContainedPath $Parent $Child)) {
    throw "$Label is outside its owned parent: $Child"
  }
}

function Test-SameOrContainedPath([string]$Parent, [string]$Candidate) {
  $parentFull = Get-FullPath $Parent
  $candidateFull = Get-FullPath $Candidate
  return ($parentFull.Equals($candidateFull, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-ContainedPath $parentFull $candidateFull))
}

function Assert-Disjoint([string]$Left, [string]$Right, [string]$Label) {
  if ((Test-SameOrContainedPath $Left $Right) -or (Test-SameOrContainedPath $Right $Left)) {
    throw "$Label paths overlap: $Left and $Right"
  }
}

function New-OwnedDirectory([string]$Name) {
  $directory = Join-Path $ownedStagingChild $Name
  Assert-ContainedChild $ownedStagingChild $directory 'Test staging directory'
  $existing = Get-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
  if ($null -ne $existing) {
    throw "Test staging directory already exists: $directory"
  }

  [void](New-Item -ItemType Directory -Path $directory -ErrorAction Stop)
  $created = Get-Item -LiteralPath $directory -Force -ErrorAction Stop
  if (-not $created.PSIsContainer -or (Test-ReparsePoint $created)) {
    throw "Test staging directory is unsafe: $directory"
  }

  return $directory
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
  $sourceFull = Assert-SafeDirectoryPath $Source 'Copy source'
  $destinationFull = Assert-SafeDirectoryPath $Destination 'Copy destination'
  Assert-ContainedChild $ownedStagingChild $destinationFull 'Test copy destination'

  if (-not (Test-Path -LiteralPath $destinationFull -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $destinationFull -ErrorAction Stop)
  }
  $destinationItem = Get-Item -LiteralPath $destinationFull -Force -ErrorAction Stop
  if (Test-ReparsePoint $destinationItem) {
    throw "Test copy destination is a reparse point: $destinationFull"
  }

  foreach ($entry in @(Get-ChildItem -LiteralPath $sourceFull -Force -ErrorAction Stop)) {
    Copy-Item -LiteralPath $entry.FullName -Destination $destinationFull -Recurse -Force -ErrorAction Stop
  }
}

function Remove-TestFile([string]$Path) {
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  if ($null -eq $item) { return }
  if ($item.PSIsContainer -or (Test-ReparsePoint $item)) {
    throw "Test marker is not a regular file: $Path"
  }

  Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
}

function Remove-OwnedTree([string]$Path) {
  if (-not (Test-SameOrContainedPath $ownedStagingChild $Path)) {
    throw "Refusing to remove path outside owned staging child: $Path"
  }

  $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  if (Test-ReparsePoint $item) {
    throw "Refusing to recurse through reparse point: $Path"
  }
  if (-not $item.PSIsContainer) {
    Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
    return
  }

  foreach ($entry in @(Get-ChildItem -LiteralPath $Path -Force -ErrorAction Stop)) {
    $currentEntry = Get-Item -LiteralPath $entry.FullName -Force -ErrorAction SilentlyContinue
    if ($null -eq $currentEntry) { continue }
    $entry = $currentEntry

    if (Test-ReparsePoint $entry) {
      Remove-Item -LiteralPath $entry.FullName -Force -ErrorAction Stop
    } elseif ($entry.PSIsContainer) {
      Remove-OwnedTree $entry.FullName
    } else {
      Remove-Item -LiteralPath $entry.FullName -Force -ErrorAction Stop
    }
  }
  Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
}

function Validate-Payload([string]$Root) {
  & $Node $manifestTool validate $Root
  if ($LASTEXITCODE -ne 0) {
    throw "Manifest validation failed for $Root"
  }
}

function Expect-ManifestFailure([string]$Label, [string]$Root) {
  & $Node $manifestTool validate $Root
  if ($LASTEXITCODE -eq 0) {
    throw "$Label unexpectedly validated"
  }
  Write-Output "$Label=PASS"
}

function Get-SampleExecutable([string]$Root, [string]$PayloadRoot) {
  $executables = @(
    Get-ChildItem -LiteralPath $Root -Filter '*.exe' -File -Force -ErrorAction Stop |
      Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $PayloadRoot $_.Name) -PathType Leaf)
      }
  )
  if ($executables.Count -ne 1) {
    throw "Expected exactly one native sample executable outside the payload in $Root, found $($executables.Count)"
  }

  $executable = Get-Item -LiteralPath $executables[0].FullName -Force -ErrorAction Stop
  if ($executable.PSIsContainer -or (Test-ReparsePoint $executable)) {
    throw "Native sample executable is unsafe: $($executable.FullName)"
  }

  return $executable.FullName
}

function Get-ExecutableDependencies([string]$Executable) {
  $file = Get-Item -LiteralPath $Executable -Force -ErrorAction Stop
  if ($file.PSIsContainer -or (Test-ReparsePoint $file)) {
    throw "Executable is unsafe: $Executable"
  }

  $output = (& $Dumpbin /DEPENDENTS $Executable 2>&1 | Out-String)
  if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect executable imports: $Executable"
  }

  return $output
}

function Assert-SelfContainedImports([string]$Executable) {
  $dependencies = Get-ExecutableDependencies $Executable
  if ($dependencies -notmatch '(?im)^\s*Microsoft\.WindowsAppRuntime\.dll\s*$') {
    throw "Executable does not import Microsoft.WindowsAppRuntime.dll: $Executable"
  }
  if ($dependencies -match '(?im)Microsoft\.WindowsAppRuntime\.Bootstrap\.dll') {
    throw "Executable imports the Bootstrap DLL: $Executable"
  }

  $bootstrap = Join-Path ([IO.Path]::GetDirectoryName($Executable)) 'Microsoft.WindowsAppRuntime.Bootstrap.dll'
  if (Test-Path -LiteralPath $bootstrap) {
    throw "Sample app ships the Bootstrap DLL: $bootstrap"
  }
}

function Assert-Marker([string]$Root, [bool]$Expected) {
  $marker = Join-Path $Root 'bare-win-ui-application-constructed.marker'
  $exists = Test-Path -LiteralPath $marker -PathType Leaf
  if ($exists -ne $Expected) {
    if ($Expected) {
      throw "Application construction marker was not created: $marker"
    }
    throw "Application construction marker was created unexpectedly: $marker"
  }
}

function New-ProcessStartInfo([string]$Executable, [string]$WorkingDirectory, [string]$UserData) {
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = $Executable
  $psi.WorkingDirectory = $WorkingDirectory
  $psi.UseShellExecute = $false
  if ($UserData) {
    $psi.EnvironmentVariables['WEBVIEW2_USER_DATA_FOLDER'] = $UserData
  }
  return $psi
}

function Run-Process([string]$Executable, [string]$WorkingDirectory, [string]$UserData) {
  $psi = New-ProcessStartInfo $Executable $WorkingDirectory $UserData
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $psi
  try {
    $started = $process.Start()
    if (-not $started) { throw "Process did not start: $Executable" }
    if (-not $process.WaitForExit(60000)) {
      [void]$process.Kill()
      throw "Process timed out: $Executable"
    }

    return $process.ExitCode
  } finally {
    $process.Dispose()
  }
}

function Run-ExpectedFailure([string]$Label, [string]$Executable, [string]$WorkingDirectory, [string]$UserData) {
  $psi = New-ProcessStartInfo $Executable $WorkingDirectory $UserData
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $psi
  $started = $false
  $result = $null
  try {
    try {
      $started = $process.Start()
    } catch [System.ComponentModel.Win32Exception] {
      $nativeErrorCode = [int]$_.Exception.NativeErrorCode
      if ($nativeErrorCode -ne 126) { throw }
      $result = [pscustomobject]@{
        Category = 'loader'
        Code = $nativeErrorCode
      }
    }

    if ($null -eq $result) {
      if (-not $started) { throw "Process did not start: $Executable" }
      if (-not $process.WaitForExit(30000)) {
        [void]$process.Kill()
        throw "$Label process timed out"
      }
      $result = [pscustomobject]@{
        Category = 'exit'
        Code = [int]$process.ExitCode
      }
      if ($result.Code -eq 0) {
        throw "$Label unexpectedly exited successfully"
      }
    }
  } finally {
    if ($started -and -not $process.HasExited) {
      try { [void]$process.Kill() } catch {}
    }
    $process.Dispose()
  }

  return $result
}

try {
  if (-not $ManifestTool) {
    $ManifestTool = Join-Path $PSScriptRoot '..\cmake\self-contained-runtime.js'
  }
  $manifestTool = Get-FullPath $ManifestTool
  $prebuildRoot = Assert-SafeDirectoryTree (Get-FullPath $PrebuildRoot) 'Prebuild root'
  $sampleSource = Assert-SafeDirectoryTree (Get-FullPath $SampleAppDirectory) 'Sample app directory'

  if (Test-Path -LiteralPath (Join-Path $prebuildRoot 'Microsoft.WindowsAppRuntime.Bootstrap.dll')) {
    throw 'x64 prebuild ships the Bootstrap DLL'
  }

  $stagingParentProvided = $PSBoundParameters.ContainsKey('StagingRoot')
  if ($stagingParentProvided) {
    if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
      throw '-StagingRoot must name an existing test-owned parent directory'
    }
    $stagingParent = Assert-SafeDirectoryPath (Get-FullPath $StagingRoot) 'Staging parent'
    $stagingParentItem = Get-Item -LiteralPath $stagingParent -Force -ErrorAction Stop
    if (-not $stagingParentItem.PSIsContainer -or (Test-ReparsePoint $stagingParentItem)) {
      throw "Staging parent is unsafe: $stagingParent"
    }
  } else {
    $stagingParent = Get-FullPath (Join-Path ([IO.Path]::GetTempPath()) "bare-win-ui-self-contained-parent-$([Guid]::NewGuid().ToString('N'))")
    [void](Assert-SafeDirectoryPath $stagingParent 'Generated staging parent')
    [void](New-Item -ItemType Directory -Path $stagingParent -ErrorAction Stop)
  }

  $stagingParentItem = Get-Item -LiteralPath $stagingParent -Force -ErrorAction Stop
  if (-not $stagingParentItem.PSIsContainer -or (Test-ReparsePoint $stagingParentItem)) {
    throw "Staging parent is unsafe: $stagingParent"
  }

  Assert-Disjoint $stagingParent $prebuildRoot 'Staging and prebuild'
  Assert-Disjoint $stagingParent $sampleSource 'Staging and sample app'

  $stagingCandidate = Join-Path $stagingParent "run-$([Guid]::NewGuid().ToString('N'))"
  Assert-ContainedChild $stagingParent $stagingCandidate 'Owned staging child'
  if ($null -ne (Get-Item -LiteralPath $stagingCandidate -Force -ErrorAction SilentlyContinue)) {
    throw "Owned staging child already exists: $stagingCandidate"
  }
  [void](New-Item -ItemType Directory -Path $stagingCandidate -ErrorAction Stop)
  $ownedStagingChild = $stagingCandidate
  $ownedItem = Get-Item -LiteralPath $ownedStagingChild -Force -ErrorAction Stop
  if (-not $ownedItem.PSIsContainer -or (Test-ReparsePoint $ownedItem)) {
    throw "Owned staging child is unsafe: $ownedStagingChild"
  }

  Validate-Payload $prebuildRoot
  Write-Output 'prebuild-manifest=PASS'

  $hardLinkPayload = New-OwnedDirectory 'hard-link-payload'
  Copy-DirectoryContents $prebuildRoot $hardLinkPayload
  New-Item -ItemType HardLink `
    -Path (Join-Path $hardLinkPayload 'linked-runtime.dll') `
    -Target (Join-Path $hardLinkPayload 'Microsoft.WindowsAppRuntime.dll') | Out-Null
  Expect-ManifestFailure 'hard-link-payload' $hardLinkPayload

  $symbolicLinkPayload = New-OwnedDirectory 'symbolic-link-payload'
  Copy-DirectoryContents $prebuildRoot $symbolicLinkPayload
  New-Item -ItemType SymbolicLink `
    -Path (Join-Path $symbolicLinkPayload 'linked-runtime.dll') `
    -Target (Join-Path $symbolicLinkPayload 'Microsoft.WindowsAppRuntime.dll') | Out-Null
  Expect-ManifestFailure 'symbolic-link-payload' $symbolicLinkPayload

  $missingPayload = New-OwnedDirectory 'missing-payload'
  Copy-DirectoryContents $prebuildRoot $missingPayload
  Remove-Item -LiteralPath (Join-Path $missingPayload 'Microsoft.WindowsAppRuntime.dll') -Force -ErrorAction Stop
  Expect-ManifestFailure 'missing-payload' $missingPayload

  $alteredPayload = New-OwnedDirectory 'altered-payload'
  Copy-DirectoryContents $prebuildRoot $alteredPayload
  [IO.File]::AppendAllText((Join-Path $alteredPayload 'Microsoft.WindowsAppRuntime.pri'), 'altered')
  Expect-ManifestFailure 'altered-payload' $alteredPayload

  $sampleApp = New-OwnedDirectory 'sample-app'
  Copy-DirectoryContents $sampleSource $sampleApp

  # The app root also contains Bare's executable/bundles and WebView2. Keep
  # the manifest boundary as a payload-only child, validate it through the
  # Node SSOT, then copy those exact bytes beside the executable.
  $samplePayload = Join-Path $sampleApp 'validated-payload'
  [void](Assert-SafeDirectoryPath $samplePayload 'Staged sample payload')
  [void](New-Item -ItemType Directory -Path $samplePayload -ErrorAction Stop)
  Copy-DirectoryContents $prebuildRoot $samplePayload
  Validate-Payload $samplePayload
  Copy-DirectoryContents $samplePayload $sampleApp
  Remove-OwnedTree $samplePayload
  Write-Output 'staged-sample-manifest=PASS'

  $sampleMarker = Join-Path $sampleApp 'bare-win-ui-application-constructed.marker'
  Remove-TestFile $sampleMarker
  $sampleExecutable = Get-SampleExecutable $sampleApp $prebuildRoot
  Assert-SelfContainedImports $sampleExecutable
  Write-Output 'launched-sample-imports=PASS'

  $healthy = $sampleApp
  $healthyMarker = Join-Path $healthy 'bare-win-ui-application-constructed.marker'
  Remove-TestFile $healthyMarker
  $healthyExecutable = Get-SampleExecutable $healthy $prebuildRoot
  Assert-SelfContainedImports $healthyExecutable
  $healthyData = Join-Path $healthy 'WebView2'
  [void](New-Item -ItemType Directory -Path $healthyData -Force -ErrorAction Stop)
  $healthyExit = Run-Process $healthyExecutable $healthy $healthyData
  if ($healthyExit -ne 0) { throw "Healthy sample exited $healthyExit" }
  Assert-Marker $healthy $true
  Write-Output 'healthy-native-sample=PASS'
  Remove-TestFile $healthyMarker
  Remove-OwnedTree $healthyData

  $missing = New-OwnedDirectory 'missing-import'
  Copy-DirectoryContents $sampleApp $missing
  $missingExecutable = Get-SampleExecutable $missing $prebuildRoot
  Assert-SelfContainedImports $missingExecutable
  Remove-Item -LiteralPath (Join-Path $missing 'Microsoft.WindowsAppRuntime.dll') -Force -ErrorAction Stop
  Remove-TestFile (Join-Path $missing 'bare-win-ui-application-constructed.marker')
  $missingData = Join-Path $missing 'WebView2'
  [void](New-Item -ItemType Directory -Path $missingData -Force -ErrorAction Stop)
  $missingResult = Run-ExpectedFailure 'missing-import-nonzero' $missingExecutable $missing $missingData
  if ($missingResult.Category -notin @('exit', 'loader')) {
    throw "Missing import returned an unknown launch category: $($missingResult.Category)"
  }
  if ($missingResult.Code -eq 0) {
    throw 'Missing imported DLL unexpectedly allowed startup'
  }
  if ($missingResult.Category -eq 'loader' -and $missingResult.Code -ne 126) {
    throw "Missing import loader error was $($missingResult.Code), expected 126"
  }
  Assert-Marker $missing $false
  Write-Output "missing-import-nonzero=PASS ($($missingResult.Category) code $($missingResult.Code))"

  $altered = New-OwnedDirectory 'altered-import'
  Copy-DirectoryContents $sampleApp $altered
  $alteredExecutable = Get-SampleExecutable $altered $prebuildRoot
  Assert-SelfContainedImports $alteredExecutable
  $runtimeDll = Join-Path $altered 'Microsoft.WindowsAppRuntime.dll'
  $runtimeBytes = [IO.File]::ReadAllBytes($runtimeDll)
  if ($runtimeBytes.Length -eq 0) { throw "Runtime DLL is empty: $runtimeDll" }
  $runtimeBytes[0] = [byte]($runtimeBytes[0] -bxor 0xff)
  [IO.File]::WriteAllBytes($runtimeDll, $runtimeBytes)
  Remove-TestFile (Join-Path $altered 'bare-win-ui-application-constructed.marker')
  $alteredData = Join-Path $altered 'WebView2'
  [void](New-Item -ItemType Directory -Path $alteredData -Force -ErrorAction Stop)
  $alteredResult = Run-ExpectedFailure 'altered-import-nonzero' $alteredExecutable $altered $alteredData
  if ($alteredResult.Category -notin @('exit', 'loader')) {
    throw "Altered import returned an unknown launch category: $($alteredResult.Category)"
  }
  if ($alteredResult.Code -eq 0) {
    throw 'Altered imported DLL unexpectedly allowed startup'
  }
  Assert-Marker $altered $false
  Write-Output "altered-import-nonzero=PASS ($($alteredResult.Category) code $($alteredResult.Code))"
} finally {
  if ($null -ne $ownedStagingChild) {
    $ownedItem = Get-Item -LiteralPath $ownedStagingChild -Force -ErrorAction SilentlyContinue
    if ($null -ne $ownedItem) {
      if (-not $ownedItem.PSIsContainer -or (Test-ReparsePoint $ownedItem)) {
        throw "Refusing to remove unsafe owned staging child: $ownedStagingChild"
      }
      Remove-OwnedTree $ownedStagingChild
    }
  }
}
