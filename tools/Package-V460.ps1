param([Parameter(Mandatory=$true)][string]$ExpectedCommit, [string]$WorkspaceRoot='D:/repos/vrdev/ofxr-autonomous')
$ErrorActionPreference = 'Stop'
$workspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$sourceRoot = Join-Path $workspaceRoot 'work/isolated/v460-interface'
$buildRoot = Join-Path $workspaceRoot 'work/shared/ofxr-autonomous/build-v460-new'
$releaseRoot = Join-Path $buildRoot 'Release'
$candidateRoot = Join-Path $workspaceRoot 'releases/candidates/ofxr-autonomous-V460'
$candidateZip = $candidateRoot + '.zip'
$checkpointRoot = Join-Path $workspaceRoot 'checkpoints/current/ofxr-autonomous/V460'
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Text([string]$path, [string]$value) {
    [IO.File]::WriteAllText($path,$value,$utf8)
}
function Relative-Workspace([string]$path) {
    $absolute = [IO.Path]::GetFullPath($path)
    $prefix = $workspaceRoot.TrimEnd('\','/') + '\'
    if (-not $absolute.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $absolute" }
    $absolute.Substring($prefix.Length).Replace('\','/')
}
function Artifact([string]$path,[string]$relative) {
    $item = Get-Item -LiteralPath $path
    [pscustomobject][ordered]@{path=$relative;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
}
foreach ($target in @($candidateRoot,$candidateZip,$checkpointRoot)) {
    $null = Relative-Workspace $target
    if (Test-Path -LiteralPath $target) { throw "Preserved destination already exists: $target" }
}
$sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $sourceCommit -ne $ExpectedCommit -or (& git -C $sourceRoot status --porcelain)) { throw 'Source must match the clean expected commit' }
$suites = @(foreach ($route in @('new','djules')) {
    $logPath = Join-Path $buildRoot "ctest-final-$route.log"
    $log = [IO.File]::ReadAllText($logPath)
    if ($log -notmatch '100% tests passed, 0 tests failed out of (\d+)') { throw "Suite not fully passed: $route" }
    $total = [int]$matches[1]
    if ($log -match '\*\*\*\s*(Not Run|Skipped|Failed)') { throw "Incomplete suite: $route" }
    if ($log -notmatch 'Total Test time \(real\) =\s+([\d.]+) sec') { throw 'Missing timing' }
    [pscustomobject]@{route=$route;passed=$total;total=$total;failed=0;skipped=0;seconds=[double]$matches[1];log_sha256=(Get-FileHash -LiteralPath $logPath).Hash}
})
$binaryPaths = @('OFXRBridgeTray.exe','ofxr/XR_APILAYER_XRFrameBridge_diagnostic.dll','ofxr/OFXR_vulkan_queue_layer.dll','ofxr/nvngx.dll_dlssnr.dll','ofxr/nvngx.dll_dlssg.dll','ofxr/ofxr_amd_fidelityfx_framegeneration_dx12.dll','ofxr/PDAFWPlugin.dll','ofxr/nvngx_dlssg.dll')
$dumpbin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe'
$imports = @(foreach ($relative in $binaryPaths) {
    $path = Join-Path $releaseRoot $relative
    $lines = @(& $dumpbin /imports $path)
    if ($LASTEXITCODE) { throw "Import inspection failed: $relative" }
    $names = @($lines | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $matches[1] } } | Sort-Object -Unique)
    if (-not $names.Count -or ($names -match 'OptiScaler')) { throw "Invalid import graph: $relative" }
    foreach ($name in $names) {
        if ($name -match '^(api-ms-|ext-ms-)') { continue }
        $choices = @((Join-Path (Split-Path $path) $name),(Join-Path $releaseRoot $name),(Join-Path $releaseRoot "ofxr/$name"),(Join-Path $env:SystemRoot "System32/$name"))
        if (-not ($choices | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })) { throw "Unresolved $name in $relative" }
    }
    [pscustomobject]@{artifact=(Artifact $path $relative);imports=$names;missing=@();optiscaler_imports=@()}
})
New-Item -ItemType Directory -Path $candidateRoot,$checkpointRoot,(Join-Path $candidateRoot 'ofxr'),(Join-Path $candidateRoot 'source'),(Join-Path $candidateRoot 'evidence') | Out-Null
foreach ($relative in $binaryPaths + @('ofxr/ofxr_bridge.ini')) {
    Copy-Item -LiteralPath (Join-Path $releaseRoot $relative) -Destination (Join-Path $candidateRoot $relative)
}
foreach ($name in @('README.md','TESTING_IT.md','VALIDATION.md','AUTONOMOUS_CONTRACT.md','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $candidateRoot
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs') -Destination (Join-Path $candidateRoot 'docs') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'licenses') -Destination (Join-Path $candidateRoot 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests/native_dlssg_baseline_report.md') -Destination (Join-Path $candidateRoot 'evidence')
Get-ChildItem -LiteralPath (Join-Path $buildRoot 'evidence') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $candidateRoot 'evidence')
}
foreach ($name in @('ctest-final-new.log','ctest-final-djules.log','runtime-new-hashes.json','runtime-djules-hashes.json')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination (Join-Path $candidateRoot 'evidence')
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs/V448_FSR_PROVIDER.json') -Destination (Join-Path $candidateRoot 'evidence/FSR_PROVIDER.json')
$sourceZip = Join-Path $checkpointRoot 'V460_SOURCE.zip'
& git -C $sourceRoot archive --format=zip "--output=$sourceZip" HEAD
if ($LASTEXITCODE) { throw 'Source archive failed' }
Copy-Item -LiteralPath $sourceZip -Destination (Join-Path $candidateRoot 'source/OFXR-Autonomous-V460-source.zip')
Write-Text (Join-Path $candidateRoot 'SOURCE.md') "# V460 source`n`nCommit: $sourceCommit`nLocal tag: xrfg/v460-autonomous.`nTracked source: source/OFXR-Autonomous-V460-source.zip.`nBuild with the prepared shared dependencies; see docs/BUILDING.md.`n"
Write-Text (Join-Path $candidateRoot 'PROVIDER_IMPORTS.json') ($imports | ConvertTo-Json -Depth 8)
$runtimeArtifacts = @($binaryPaths + @('ofxr/ofxr_bridge.ini') | ForEach-Object { Artifact (Join-Path $candidateRoot $_) $_ })
$provenance = [ordered]@{
    schema_version=1; version='V460'; source_commit=$sourceCommit; source_tag='xrfg/v460-autonomous'
    source_workspace=(Relative-Workspace $sourceRoot); created_at_utc=[DateTime]::UtcNow.ToString('o')
    status='isolated-test-candidate-runtime-pending'; configuration='MSVC VS2022 x64 optimized Release'
    parent_v456='a1fcf74e123f825eefd998f558907cb702212718'; renderer_guides_provider_pacing='V456 preserved'; integration='shared desktop/headset UI and visibility-gated telemetry'; suites=$suites
    change='Four counters, three independently sourced timings, actual vectors and detections, existing controls and independent corner counter'
    regression='Real identity, success/failure/repeat/native receipt, stale clocks, simultaneous detections, real D11/D12 menu readbacks and hidden visibility gate'
    game_or_headset_validation='Automated GPU/runtime fixtures passed; owner live game/headset confirmation pending'
    ipc_abi=46001; generation_profiles=6; diagnostic_logging='live-in-session'
    startup='disarmed'; profile='%LOCALAPPDATA%/OFXR Autonomous'; live_installation=$false; tray_activation_or_registration=$false
    external_nr_model_included=$false; product_or_canonical_port=$false; push_or_publication=$false
    runtime_artifacts=$runtimeArtifacts; source_archive=(Artifact $sourceZip 'source/OFXR-Autonomous-V460-source.zip')
}
Write-Text (Join-Path $candidateRoot 'BUILD_PROVENANCE.json') ($provenance | ConvertTo-Json -Depth 12)
$checksums = @(Get-ChildItem -LiteralPath $candidateRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() + '  ' + $_.FullName.Substring($candidateRoot.Length+1).Replace('\','/')
})
Write-Text (Join-Path $candidateRoot 'SHA256SUMS.txt') (($checksums -join "`n")+"`n")
foreach ($entry in @(@('OFXRBridgeTray.exe','v460-OFXRBridgeTray.exe'),@('ofxr/XR_APILAYER_XRFrameBridge_diagnostic.dll','v460-OFXR-Autonomous.dll'),@('ofxr/OFXR_vulkan_queue_layer.dll','v460-OFXR_vulkan_queue_layer.dll'),@('ofxr/nvngx.dll_dlssnr.dll','v460-nvngx.dll_dlssnr.dll'),@('ofxr/nvngx.dll_dlssg.dll','v460-nvngx.dll_dlssg.dll'))) {
    Copy-Item -LiteralPath (Join-Path $candidateRoot $entry[0]) -Destination (Join-Path $checkpointRoot $entry[1])
}
Copy-Item -LiteralPath (Join-Path $candidateRoot 'BUILD_PROVENANCE.json') -Destination (Join-Path $checkpointRoot 'V460_PROVENANCE.json')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs/V460_INTERFACE.md') -Destination (Join-Path $checkpointRoot 'V460_NOTES.md')
$checkpointArtifacts = @(Get-ChildItem -LiteralPath $checkpointRoot -File | Sort-Object Name | ForEach-Object { Artifact $_.FullName (Relative-Workspace $_.FullName) })
Write-Text (Join-Path $checkpointRoot 'V460_MANIFEST.json') ([ordered]@{schema_version=1;version='V460';source_commit=$sourceCommit;status='runtime-pending';artifacts=$checkpointArtifacts} | ConvertTo-Json -Depth 8)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($candidateRoot,$candidateZip,[IO.Compression.CompressionLevel]::Optimal,$true)
$package = Artifact $candidateZip (Relative-Workspace $candidateZip)
Write-Text (Join-Path $checkpointRoot 'V460_DELIVERY.json') ([ordered]@{schema_version=1;version='V460';source_workspace=(Relative-Workspace $sourceRoot);source_commit=$sourceCommit;package=$package;checkpoint=(Relative-Workspace $checkpointRoot);suites=$suites} | ConvertTo-Json -Depth 8)
$catalogRows = @(Get-ChildItem -LiteralPath $checkpointRoot -File) + @(Get-Item -LiteralPath $candidateZip)
$newRows = @($catalogRows | ForEach-Object {
    $relative = Relative-Workspace $_.FullName
    [pscustomobject][ordered]@{Line='ofxr-autonomous';Version='V460';LegacyPath=$relative;CurrentPath=$relative;SizeBytes=$_.Length;Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;CreationTimeUtc=$_.CreationTimeUtc.ToString('o');LastWriteTimeUtc=$_.LastWriteTimeUtc.ToString('o');LastAccessTimeUtc=$_.LastAccessTimeUtc.ToString('o');Attributes=[int]$_.Attributes}
})
$catalogPath = Join-Path $workspaceRoot 'catalog/CURRENT_CHECKPOINTS.csv'
if (@(Import-Csv -LiteralPath $catalogPath | Where-Object Version -eq 'V460').Count) { throw 'V460 already cataloged' }
$append = @($newRows | ConvertTo-Csv -NoTypeInformation | Select-Object -Skip 1)
[IO.File]::AppendAllText($catalogPath,($append -join "`r`n")+"`r`n",$utf8)
# The migration completion marker is historical evidence; current builds have a separate receipt.
# V460 is a parallel UI candidate. Preserve the Vulkan chat's selected baseline/current pointer.
$package | ConvertTo-Json -Compress
