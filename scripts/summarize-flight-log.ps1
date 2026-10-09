param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [ValidateRange(1, 100)]
    [int]$Top = 20
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Average {
    param([object[]]$Values)
    [Math]::Round(($Values | Measure-Object -Average).Average, 1)
}

function Get-Percentile {
    param([double[]]$Values, [double]$Fraction)
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Min($sorted.Count - 1, [int][Math]::Floor($Fraction * $sorted.Count))
    [Math]::Round($sorted[$index], 2)
}

$resolved = (Resolve-Path -LiteralPath $LogPath).Path
$pending = @{}
$completed = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[object]]::new()
$decisions = [System.Collections.Generic.List[object]]::new()
$nvidiaStages = [System.Collections.Generic.List[object]]::new()
$nvidiaTotals = [System.Collections.Generic.List[object]]::new()
# Generation timing: where a frame's synthesis sits against the application's
# xrEndFrame, and what the presenter handed over. Written for comparing two
# captures of one scene, `capture_at_end_frame` on and off.
$endFrameBegins = [System.Collections.Generic.List[double]]::new()
$endFrameDurationsMs = [System.Collections.Generic.List[double]]::new()
$gpuSpans = [System.Collections.Generic.List[object]]::new()
$pendingReleases = [System.Collections.Generic.List[object]]::new()
$releaseToEndMs = @{}
$submissions = @{ 0 = 0; 1 = 0; 2 = 0 }
$submissionFirstMs = $null
$submissionLastMs = $null
$holds = @{ 400 = 0; 401 = 0; 402 = 0 }
$deferredCaptures = 0
$deferredCaptureFailures = 0
$bridgedReleaseDelays = [System.Collections.Generic.List[double]]::new()
$bridgedReleaseFailures = 0
$delivered = [System.Collections.Generic.List[double]]::new()
$lineNumber = 0

Get-Content -LiteralPath $resolved | ForEach-Object {
    ++$lineNumber
    $line = $_
    if ($line -notmatch
        '^seq=(\d+) ms=([0-9.]+) tid=(\d+) phase=([BEI]) op=([^ ]+) result=(-?\d+) dur_us=(\d+) a=(\d+) b=(\d+) c=(\d+)') {
        return
    }

    $sequence = [UInt64]$Matches[1]
    $entry = [pscustomobject]@{
        Sequence = $sequence
        Milliseconds = [double]$Matches[2]
        Thread = [UInt32]$Matches[3]
        Phase = $Matches[4]
        Operation = $Matches[5]
        Result = [Int64]$Matches[6]
        DurationUs = [UInt64]$Matches[7]
        A = [UInt64]$Matches[8]
        B = [UInt64]$Matches[9]
        C = [UInt64]$Matches[10]
        LineNumber = $lineNumber
        Line = $line
    }

    switch ($entry.Phase) {
        'B' {
            $pending[$sequence] = $entry
            if ($entry.Operation -eq 'app_end_frame') {
                $endFrameBegins.Add($entry.Milliseconds)
                # Each image released since the last xrEndFrame: how long
                # before this one its release returned. The first-released
                # eye has the longer gap.
                foreach ($release in $pendingReleases) {
                    if (-not $releaseToEndMs.ContainsKey($release.Swapchain)) {
                        $releaseToEndMs[$release.Swapchain] =
                            [System.Collections.Generic.List[double]]::new()
                    }
                    $releaseToEndMs[$release.Swapchain].Add(
                        $entry.Milliseconds - $release.Milliseconds)
                }
                $pendingReleases.Clear()
            }
        }
        'E' {
            [void]$pending.Remove($sequence)
            $completed.Add($entry)
            if ($entry.Result -lt 0) {
                $failures.Add($entry)
            }
            if ($entry.Operation -eq 'app_end_frame') {
                $endFrameDurationsMs.Add($entry.DurationUs / 1000.0)
            }
            if ($entry.Operation -eq 'app_swapchain_release' -and
                $entry.Result -ge 0) {
                $pendingReleases.Add([pscustomobject]@{
                    Swapchain = $entry.A
                    Milliseconds = $entry.Milliseconds
                })
            }
        }
        'I' {
            # presenter_vsync_lock carries a signed phase in result, not a
            # status: a negative one is not a failure.
            if ($entry.Result -lt 0 -and
                $entry.Operation -ne 'presenter_vsync_lock') {
                $failures.Add($entry)
            }
            if ($entry.Operation -in @(
                    'swapchain_eligibility',
                    'projection_mapping',
                    'generation_prepare')) {
                $decisions.Add($entry)
            }
            if ($entry.Operation -eq 'nvidia_gpu_stages' -and
                $entry.Result -ge 0) {
                $nvidiaStages.Add($entry)
            }
            if ($entry.Operation -eq 'nvidia_gpu_total' -and
                $entry.Result -eq 0) {
                $nvidiaTotals.Add($entry)
            }
            # a and b are the GPU's begin and end of the pair's synthesis in
            # microseconds on this log's timeline; both zero until measured.
            if ($entry.Operation -eq 'synthesis_gpu_span' -and
                $entry.A -gt 0 -and $entry.B -ge $entry.A) {
                $gpuSpans.Add([pscustomobject]@{
                    BeginMs = $entry.A / 1000.0
                    EndMs = $entry.B / 1000.0
                })
            }
            if ($entry.Operation -eq 'presenter_submission') {
                if ($submissions.ContainsKey([int]$entry.C)) {
                    $submissions[[int]$entry.C]++
                }
                if ($null -eq $submissionFirstMs) {
                    $submissionFirstMs = $entry.Milliseconds
                }
                $submissionLastMs = $entry.Milliseconds
            }
            if ($entry.Operation -eq 'presenter_transition' -and
                $holds.ContainsKey([int]$entry.Result)) {
                $holds[[int]$entry.Result]++
            }
            if ($entry.Operation -eq 'deferred_capture') {
                $deferredCaptures++
                if ($entry.Result -lt 0) { $deferredCaptureFailures++ }
            }
            # D3D11 bridge stage 4: a release deferred to xrEndFrame ran,
            # c microseconds after the game's release; a failed one carries
            # the runtime's code with b 4.
            if ($entry.Operation -eq 'd3d11_bridge') {
                if ($entry.Result -eq 4) {
                    $bridgedReleaseDelays.Add($entry.C / 1000.0)
                } elseif ($entry.Result -lt 0 -and $entry.B -eq 4) {
                    $bridgedReleaseFailures++
                }
            }
            if ($entry.Operation -eq 'steamvr_delivery') {
                $delivered.Add($entry.A / 1000.0)
            }
        }
    }
}

Write-Output "OFXR flight log: $resolved"
Write-Output "Completed boundaries: $($completed.Count)"
Write-Output "Failed boundaries/events: $($failures.Count)"
Write-Output "Unmatched BEGIN boundaries: $($pending.Count)"

if ($pending.Count -gt 0) {
    Write-Output ''
    Write-Output 'Unmatched BEGIN boundaries (probable hang location):'
    $pending.Values |
        Sort-Object Sequence |
        Select-Object Sequence, Milliseconds, Thread, Operation, LineNumber, Line |
        Format-Table -AutoSize -Wrap
}

if ($failures.Count -gt 0) {
    Write-Output ''
    Write-Output 'Failures:'
    $failures |
        Select-Object Sequence, Milliseconds, Thread, Operation, Result,
            DurationUs, LineNumber |
        Format-Table -AutoSize
}

if ($decisions.Count -gt 0) {
    Write-Output ''
    Write-Output 'Eligibility decisions:'
    $decisions |
        Group-Object Operation, Result |
        Sort-Object Name |
        Select-Object Count, Name |
        Format-Table -AutoSize
}

if ($nvidiaStages.Count -gt 0 -and $nvidiaTotals.Count -gt 0) {
    Write-Output ''
    Write-Output "NVIDIA GPU timings ($($nvidiaTotals.Count) completed pairs, microseconds):"
    [pscustomobject]@{
        PackAvg = Get-Average -Values @($nvidiaStages | ForEach-Object A)
        Eye0Avg = Get-Average -Values @($nvidiaStages | ForEach-Object B)
        Eye1Avg = Get-Average -Values @($nvidiaStages | ForEach-Object C)
        CompositionAvg = Get-Average -Values @($nvidiaTotals | ForEach-Object A)
        TotalAvg = Get-Average -Values @($nvidiaTotals | ForEach-Object B)
        TotalMax = ($nvidiaTotals | Measure-Object B -Maximum).Maximum
    } | Format-Table -AutoSize
}

if ($endFrameBegins.Count -gt 0) {
    Write-Output ''
    Write-Output 'Generation timing (compare two captures of one scene):'
    Write-Output ("  application xrEndFrame: {0} calls, duration p50 {1} ms, p90 {2} ms" -f
        $endFrameDurationsMs.Count,
        (Get-Percentile -Values $endFrameDurationsMs.ToArray() -Fraction 0.5),
        (Get-Percentile -Values $endFrameDurationsMs.ToArray() -Fraction 0.9))

    foreach ($swapchain in ($releaseToEndMs.Keys | Sort-Object)) {
        $gaps = $releaseToEndMs[$swapchain].ToArray()
        Write-Output ("  swapchain {0}: released {1} ms before xrEndFrame (p50), {2} ms (p90), {3} releases" -f
            $swapchain,
            (Get-Percentile -Values $gaps -Fraction 0.5),
            (Get-Percentile -Values $gaps -Fraction 0.9),
            $gaps.Count)
    }

    if ($gpuSpans.Count -gt 0) {
        # Each span against the xrEndFrame that queued it: the last one that
        # began before the GPU started on the pair.
        $begins = $endFrameBegins.ToArray()
        $startLag = [System.Collections.Generic.List[double]]::new()
        $endLag = [System.Collections.Generic.List[double]]::new()
        foreach ($span in $gpuSpans) {
            $index = [Array]::BinarySearch($begins, $span.BeginMs)
            if ($index -lt 0) { $index = (-bnot $index) - 1 }
            if ($index -lt 0) { continue }
            $lag = $span.BeginMs - $begins[$index]
            # A span further than this from any xrEndFrame is from before a
            # pause or a stall and says nothing about the steady state.
            if ($lag -gt 200) { continue }
            $startLag.Add($lag)
            $endLag.Add($span.EndMs - $begins[$index])
        }
        Write-Output ("  synthesis starts on the GPU {0} ms after xrEndFrame begins (p50), {1} ms (p90), {2} pairs" -f
            (Get-Percentile -Values $startLag.ToArray() -Fraction 0.5),
            (Get-Percentile -Values $startLag.ToArray() -Fraction 0.9),
            $startLag.Count)
        Write-Output ("  synthesis is finished {0} ms after xrEndFrame begins (p50), {1} ms (p90)" -f
            (Get-Percentile -Values $endLag.ToArray() -Fraction 0.5),
            (Get-Percentile -Values $endLag.ToArray() -Fraction 0.9))
    } else {
        Write-Output '  no synthesis_gpu_span values: the recorder was not writing GPU timings'
    }

    $handedOver = $submissions[0] + $submissions[1] + $submissions[2]
    if ($handedOver -gt 0 -and $submissionLastMs -gt $submissionFirstMs) {
        $seconds = ($submissionLastMs - $submissionFirstMs) / 1000.0
        Write-Output ("  presenter over {0:N1} s: {1:N1} synthetic/s, {2:N1} real/s, {3:N2} repeats/s ({4} repeats)" -f
            $seconds,
            ($submissions[2] / $seconds),
            ($submissions[1] / $seconds),
            ($submissions[0] / $seconds),
            $submissions[0])
        Write-Output ("  synthetics held: {0} for age (400), {1} unwritten (401), {2} would have been held (402)" -f
            $holds[400], $holds[401], $holds[402])
    } else {
        Write-Output '  no presenter submissions: the session ran inline'
    }

    if ($deferredCaptures -gt 0) {
        Write-Output ("  end-frame captures: {0}, of which {1} failed" -f
            $deferredCaptures, $deferredCaptureFailures)
    }
    if ($bridgedReleaseDelays.Count -gt 0 -or $bridgedReleaseFailures -gt 0) {
        $sortedDelays = @($bridgedReleaseDelays | Sort-Object)
        $p50 = if ($sortedDelays.Count -gt 0) { $sortedDelays[[int][Math]::Floor(0.5 * ($sortedDelays.Count - 1))] } else { 0 }
        $p90 = if ($sortedDelays.Count -gt 0) { $sortedDelays[[int][Math]::Floor(0.9 * ($sortedDelays.Count - 1))] } else { 0 }
        Write-Output ("  bridged releases at xrEndFrame: {0}, of which {1} failed; {2:N2} ms after the game's release (p50), {3:N2} ms (p90)" -f
            ($bridgedReleaseDelays.Count + $bridgedReleaseFailures), $bridgedReleaseFailures, $p50, $p90)
    }
    if ($deferredCaptures -eq 0 -and $bridgedReleaseDelays.Count -eq 0 -and $bridgedReleaseFailures -eq 0) {
        Write-Output '  no end-frame captures: capture_at_end_frame was off, or the build predates V440 on a bridged session'
    }

    if ($delivered.Count -gt 0) {
        Write-Output ("  SteamVR delivered {0} frames/s (p50), {1} (p10), over {2} windows" -f
            (Get-Percentile -Values $delivered.ToArray() -Fraction 0.5),
            (Get-Percentile -Values $delivered.ToArray() -Fraction 0.1),
            $delivered.Count)
    }
}

Write-Output ''
Write-Output "Slowest $Top completed boundaries:"
$completed |
    Sort-Object DurationUs -Descending |
    Select-Object -First $Top Sequence, Milliseconds, Thread, Operation,
        Result, DurationUs, LineNumber |
    Format-Table -AutoSize
