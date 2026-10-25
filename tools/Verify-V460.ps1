param([Parameter(Mandatory=$true)][string]$ExpectedCommit, [string]$WorkspaceRoot='D:/repos/vrdev/ofxr-autonomous')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
$build = Join-Path $workspace 'work/shared/ofxr-autonomous/build-v460-new'
$package = Join-Path $workspace 'releases/candidates/ofxr-autonomous-V460.zip'
$smoke = Join-Path $workspace ("work/shared/ofxr-autonomous/package-smoke-v460-" + $ExpectedCommit.Substring(0,7))
if (Test-Path -LiteralPath $smoke) { throw 'Verification destination already exists' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($package,$smoke)
$payload = Join-Path $smoke 'ofxr-autonomous-V460'
$provider = Get-Content -LiteralPath (Join-Path $payload 'docs/V448_FSR_PROVIDER.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $payload 'ofxr/ofxr_amd_fidelityfx_framegeneration_dx12.dll')).Hash.ToLowerInvariant() -ne $provider.v448.sha256 -or $provider.v448.api_provider -ne '1.1.3') { throw 'Wrong FSR provider' }
$checked = 0
foreach ($line in Get-Content -LiteralPath (Join-Path $payload 'SHA256SUMS.txt')) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum row' }
    $expected = $matches[1]; $relative = $matches[2]
    $path = [IO.Path]::GetFullPath((Join-Path $payload $relative))
    if (-not $path.StartsWith($payload + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escapes package' }
    if ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $expected) { throw "Checksum mismatch: $relative" }
    $checked++
}
$provenance = Get-Content -LiteralPath (Join-Path $payload 'BUILD_PROVENANCE.json') -Raw | ConvertFrom-Json
if ($provenance.source_commit -ne $ExpectedCommit) { throw 'Wrong packaged source' }
foreach ($artifact in $provenance.runtime_artifacts) {
    if ((Get-FileHash -LiteralPath (Join-Path $build ("Release/" + $artifact.path))).Hash.ToLowerInvariant() -ne $artifact.sha256) {
        throw "Packaged runtime differs from tested build: $($artifact.path)"
    }
}
if ((Get-Content -LiteralPath (Join-Path $payload 'ofxr/ofxr_bridge.ini') -Raw) -notmatch '(?m)^single_swapchain_rings=0\s*$') { throw 'Wrong Lower VRAM default' }
foreach ($row in Import-Csv -LiteralPath (Join-Path $workspace 'catalog/CURRENT_CHECKPOINTS.csv')) {
    if ((Get-FileHash -LiteralPath (Join-Path $workspace $row.CurrentPath)).Hash -ne $row.Sha256) { throw "Catalog mismatch: $($row.CurrentPath)" }
}
& (Join-Path $build 'Release/xrfg_layer_call_chain.exe') (Join-Path $payload 'ofxr/XR_APILAYER_XRFrameBridge_diagnostic.dll') (Join-Path $smoke 'split-eye.log') split-eye > (Join-Path $smoke 'probe-output.log') 2>&1
if ($LASTEXITCODE) { throw "Packaged split-eye loader probe failed: $LASTEXITCODE" }
$receipt = [ordered]@{version='V460';source_commit=$ExpectedCommit;checked_files=$checked;runtime_matches_tested_build=$true;catalog_verified=$true;lower_vram_default=0;packaged_split_eye_probe='passed';package_sha256=(Get-FileHash -LiteralPath $package).Hash.ToLowerInvariant();live_activation=$false;fsr_api_provider='1.1.3';game_validation='Owner live game/headset UI confirmation pending; V456 renderer retained'}
[IO.File]::WriteAllText((Join-Path $smoke 'VERIFICATION.json'),($receipt|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$receipt | ConvertTo-Json
