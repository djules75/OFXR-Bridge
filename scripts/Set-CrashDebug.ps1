param([switch]$Enable,[string]$GamePath='',
    [string]$StateDirectory=(Join-Path $env:LOCALAPPDATA 'ofxr-bridge/CrashDiagnostics'))
$ErrorActionPreference='Stop'

function Set-OfxrCrashDebug {
    param([bool]$Enabled,[string]$Game,[string]$Directory,[scriptblock]$Runner)
    $journal=Join-Path $Directory 'dred-state.json'
    if (!$Enabled -and !(Test-Path -LiteralPath $journal)) { return }
    [IO.Directory]::CreateDirectory($Directory) | Out-Null
    $status=Join-Path $Directory 'readiness.txt'
    $keys=@('auto-breadcrumbs','breadcrumb-contexts','page-faults')
    $call={ param([string[]]$Arguments) @(& $Runner $Arguments) }
    $readDred={
        $values=@{}
        foreach($line in (& $call @('dred'))) {
            if("$line" -match '^([a-z-]+)=(system-controlled|forced-on|forced-off)$') {
                $values[$Matches[1]]=$Matches[2]
            }
        }
        foreach($key in $keys) { if(!$values.ContainsKey($key)){throw "Missing DRED setting $key"} }
        $values
    }
    $readApps={
        @(& $call @('apps') | ForEach-Object { "$($_)".Trim().ToLowerInvariant() } |
            Where-Object { $_ -and $_ -ne 'apps' -and $_ -notmatch '^[-<]' -and $_ -notmatch '^warning:|^see ' })
    }
    try {
        $saved=if(Test-Path -LiteralPath $journal){Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json}else{$null}
        if(!$Enabled) {
            foreach($app in @($saved.owned)) {
                if($app -in @(& $readApps)) { & $call @('apps','--remove',"$app") | Out-Null }
            }
            $current=& $readDred
            foreach($key in $keys) {
                # Do not overwrite a setting another tool changed after us.
                if($current[$key] -eq 'forced-on') {
                    & $call @('dred',"$key=$($saved.original.$key)") | Out-Null
                }
            }
            Remove-Item -LiteralPath $journal
            [IO.File]::WriteAllText($status,"Crash debug OFF; owned registrations removed; previous DRED settings restored.`n")
            return
        }
        if(!$Game) {
            [IO.File]::WriteAllText($status,"Crash recorder ON; DRED waiting for game discovery. First discovered game needs restart.`n")
            return
        }
        $target=[IO.Path]::GetFullPath($Game).ToLowerInvariant()
        if([IO.Path]::GetExtension($target) -ne '.exe' -or !(Test-Path -LiteralPath $target -PathType Leaf)) {
            throw 'DRED target must be an existing game executable'
        }
        $current=& $readDred
        $apps=@(& $readApps)
        $owned=if($saved){@($saved.owned)}else{@()}
        $foreign=@($apps | Where-Object { $_ -notin $owned -and $_ -ne $target })
        if($foreign.Count -and @($keys | Where-Object {$current[$_] -ne 'forced-on'}).Count) {
            throw 'DRED setup blocked: other applications already use different D3DConfig settings; no foreign settings changed'
        }
        if(!$saved) { $saved=[pscustomobject]@{original=$current;owned=@()} }
        if($target -notin $apps -and $target -notin @($saved.owned)) {
            $saved.owned=@($saved.owned)+@($target)
        }
        # Persist ownership and original values BEFORE any system mutation.
        [IO.File]::WriteAllText($journal,($saved | ConvertTo-Json -Depth 5))
        foreach($key in $keys) {
            if($current[$key] -ne 'forced-on'){& $call @('dred',"$key=forced-on") | Out-Null}
        }
        if($target -notin $apps){& $call @('apps','--add',$target) | Out-Null}
        $verified=& $readDred
        if($target -notin @(& $readApps) -or @($keys | Where-Object {$verified[$_] -ne 'forced-on'}).Count) {
            throw 'DRED verification failed'
        }
        # A receipt is a request, not a claim that an already-created device changed.
        [IO.File]::WriteAllText($status,"Crash recorder ON; DRED prepared. Restart any running game.`nTarget: $target`nPrepared UTC: $([DateTime]::UtcNow.ToString('o'))`n")
    } catch {
        $label=if($Enabled){'Crash recorder ON; DRED NOT READY'}else{'Crash debug OFF; DRED cleanup incomplete'}
        [IO.File]::WriteAllText($status,"${label}: $($_.Exception.Message)`n")
        throw
    }
}

if($MyInvocation.InvocationName -ne '.') {
    $mutex=[Threading.Mutex]::new($false,'Local\OFXRBridgeCrashDebugConfiguration')
    $locked=$false
    try {
        $locked=$mutex.WaitOne(5000)
        if(!$locked){throw 'Crash debug configuration busy'}
        $tool=Join-Path ([Environment]::SystemDirectory) 'd3dconfig.exe'
        Set-OfxrCrashDebug -Enabled ([bool]$Enable) -Game $GamePath -Directory $StateDirectory -Runner {
            param($Arguments)
            $output=@(& $tool @Arguments 2>&1)
            if($LASTEXITCODE){throw "D3DConfig failed ($LASTEXITCODE): $($output -join ' ')"}
            $output
        }
        exit 0
    } catch {
        Write-Error $_
        exit 1
    } finally {
        if($locked){$mutex.ReleaseMutex()}
        $mutex.Dispose()
    }
}
