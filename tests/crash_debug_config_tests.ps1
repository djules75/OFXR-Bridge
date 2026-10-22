param([string]$Helper)
$ErrorActionPreference='Stop'
. $Helper
$root=Join-Path ([IO.Path]::GetTempPath()) "ofxr-crash-config-test-$PID-$([Guid]::NewGuid())"
[IO.Directory]::CreateDirectory($root) | Out-Null
$game=Join-Path $root 'Test Game.exe';[IO.File]::WriteAllText($game,'fixture')
$other=Join-Path $root 'Other Game.exe';[IO.File]::WriteAllText($other,'fixture')
$script:state=@{apps=@();dred=@{'auto-breadcrumbs'='system-controlled';'breadcrumb-contexts'='forced-off';'page-faults'='system-controlled';'watson-dumps'='forced-off'};calls=0}
$runner={param($Arguments)
    ++$script:state.calls
    if($Arguments[0] -eq 'apps') {
        if($Arguments.Count -eq 1){'apps';'----------------';if(!$script:state.apps.Count){'<no apps>'}else{$script:state.apps};return}
        if($Arguments[1] -eq '--add'){$script:state.apps=@($script:state.apps)+@($Arguments[2]);return}
        if($Arguments[1] -eq '--remove'){$script:state.apps=@($script:state.apps | Where-Object {$_ -ne $Arguments[2]});return}
    }
    if($Arguments[0] -eq 'dred') {
        if($Arguments.Count -eq 1){foreach($key in $script:state.dred.Keys){"$key=$($script:state.dred[$key])"};return}
        $parts=$Arguments[1].Split('=');$script:state.dred[$parts[0]]=$parts[1];return
    }
    throw "Unexpected command: $($Arguments -join '|')"
}
function Check([bool]$Ok,[string]$Message){if(!$Ok){throw $Message}}
$folder=Join-Path $root 'state'
Set-OfxrCrashDebug $false '' $folder $runner
Check ($script:state.calls -eq 0) 'Default OFF invoked configuration tools'
Set-OfxrCrashDebug $true $game $folder $runner
Check ($script:state.apps.Count -eq 1) 'Game registration missing'
Check ($script:state.dred['auto-breadcrumbs'] -eq 'forced-on') 'Breadcrumbs not armed'
Check ($script:state.dred['watson-dumps'] -eq 'forced-off') 'Watson setting changed'
Set-OfxrCrashDebug $true $game $folder $runner
Check ($script:state.apps.Count -eq 1) 'Duplicate registration'
Set-OfxrCrashDebug $true $other $folder $runner
Check ($script:state.apps.Count -eq 2) 'Second game registration missing'
# New foreign registration and an independently changed setting must survive OFF.
$script:state.apps+=@('foreign.exe');$script:state.dred['page-faults']='forced-off'
Set-OfxrCrashDebug $false '' $folder $runner
Check ($script:state.apps.Count -eq 1 -and $script:state.apps[0] -eq 'foreign.exe') 'Foreign app removed'
Check ($script:state.dred['auto-breadcrumbs'] -eq 'system-controlled') 'Original breadcrumbs not restored'
Check ($script:state.dred['breadcrumb-contexts'] -eq 'forced-off') 'Original contexts not restored'
Check ($script:state.dred['page-faults'] -eq 'forced-off') 'Independent page-fault edit overwritten'
Check (!(Test-Path -LiteralPath (Join-Path $folder 'dred-state.json'))) 'Ownership journal not retired'
$before=$script:state.dred.Clone();$rejected=$false
try{Set-OfxrCrashDebug $true $game (Join-Path $root 'foreign') $runner}catch{$rejected=$true}
Check $rejected 'Conflicting foreign D3DConfig application admitted'
foreach($key in $before.Keys){Check ($before[$key] -eq $script:state.dred[$key]) 'Foreign DRED setting modified'}
# A partial tool failure retains a recoverable ownership/original-state journal.
$script:state.apps=@();$script:failOnce=$true
$script:baseRunner=$runner
$failing={param($Arguments)
    if($script:failOnce -and $Arguments[0] -eq 'dred' -and $Arguments.Count -gt 1 -and
       $Arguments[1].StartsWith('breadcrumb-contexts=')) {
        $script:failOnce=$false;throw 'simulated tool failure'
    }
    & $script:baseRunner $Arguments
}
$partial=Join-Path $root 'partial';$failed=$false
try{Set-OfxrCrashDebug $true $game $partial $failing}catch{$failed=$true}
Check ($failed -and (Test-Path -LiteralPath (Join-Path $partial 'dred-state.json'))) 'Partial setup lost recovery journal'
Set-OfxrCrashDebug $false '' $partial $runner
foreach($key in $before.Keys){Check ($before[$key] -eq $script:state.dred[$key]) 'Partial setup cleanup did not restore original values'}
Write-Output 'PASS crash debug default-off, game registration, DRED arming, exact cleanup and foreign-state protection'
