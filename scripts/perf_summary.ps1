<#
Summarise the [framegen] stats lines the plugin writes every N seconds (xr_framegen_log=N) from
cyberpunkvrport.log: real/generated/output fps, CPU and GPU frame time, 1% lows, repeats.
Usage: pwsh scripts\perf_summary.ps1 [-Log <path>] [-Last 60]
#>
param(
    [string]$Log = "C:\Games\Steam\steamapps\common\Cyberpunk 2077\bin\x64\cyberpunkvrport.log",
    [int]$Last = 0
)
$lines = Get-Content $Log | Select-String '\[framegen\] stats:' | ForEach-Object { $_.Line }
if ($Last -gt 0) { $lines = $lines | Select-Object -Last $Last }
if (-not $lines) { Write-Host "no [framegen] stats lines in $Log (is xr_framegen_log set, and did the session run long enough?)"; exit 1 }
$rows = foreach ($l in $lines) {
    if ($l -match 'real ([\d.]+) fg ([\d.]+) out ([\d.]+) fps \| (\d+)s avg ([\d.]+) min ([\d.]+) 1%low ([\d.]+) 0\.1%low ([\d.]+) \| cpu ([\d.]+) ms \(avg ([\d.]+) p99 ([\d.]+)\) gpu ([\d.]+) ms \(avg ([\d.]+) p99 ([\d.]+)\) budget ([\d.]+) \| gen ([\d.]+) ms repeat ([\d.]+)% skipped (\d+)') {
        $row = [pscustomobject]@{ real=[double]$Matches[1]; fg=[double]$Matches[2]; out=[double]$Matches[3]; low1=[double]$Matches[7]; cpuAvg=[double]$Matches[10]; cpuP99=[double]$Matches[11]; gpuAvg=[double]$Matches[13]; gpuP99=[double]$Matches[14]; budget=[double]$Matches[15]; repeat=[double]$Matches[17]; gpuPct=0.0; cpuPct=0.0; thread=0.0; fgState='' }
        if ($l -match 'fg (\w+/\w+) "([^"]*)" \| gpu ([\d.]+)% cpu ([\d.]+)% busiest thread ([\d.]+)%') { $row.fgState = "$($Matches[1]) $($Matches[2])"; $row.gpuPct=[double]$Matches[3]; $row.cpuPct=[double]$Matches[4]; $row.thread=[double]$Matches[5] }
        $row
    }
}
$rows = @($rows | Where-Object { $_.real -gt 1 })   # drop the warm-up/menu samples
$n = $rows.Count
$avg = { param($p) [math]::Round(($rows | Measure-Object $p -Average).Average, 1) }
$min = { param($p) [math]::Round(($rows | Measure-Object $p -Minimum).Minimum, 1) }
$max = { param($p) [math]::Round(($rows | Measure-Object $p -Maximum).Maximum, 1) }
Write-Host ("samples: {0}   frame budget: {1} ms" -f $n, (& $avg 'budget'))
Write-Host ("real fps     avg {0}  min {1}  (1% low avg {2})" -f (& $avg 'real'), (& $min 'real'), (& $avg 'low1'))
Write-Host ("output fps   avg {0}  (generated avg {1})" -f (& $avg 'out'), (& $avg 'fg'))
Write-Host ("CPU ms       avg {0}  p99 avg {1}  worst p99 {2}" -f (& $avg 'cpuAvg'), (& $avg 'cpuP99'), (& $max 'cpuP99'))
Write-Host ("GPU ms       avg {0}  p99 avg {1}  worst p99 {2}" -f (& $avg 'gpuAvg'), (& $avg 'gpuP99'), (& $max 'gpuP99'))
Write-Host ("repeated frames {0}% avg, {1}% worst" -f (& $avg 'repeat'), (& $max 'repeat'))
$states = ($rows | Group-Object fgState | Sort-Object Count -Descending | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join '; '
Write-Host ("framegen states: {0}" -f $states)
Write-Host ("GPU load {0}% avg (max {1}%)   busiest thread {2}% of one core avg (max {3}%)" -f (& $avg 'gpuPct'), (& $max 'gpuPct'), (& $avg 'thread'), (& $max 'thread'))
$bound = if ((& $avg 'thread') -ge 85 -and (& $avg 'gpuPct') -lt 85) { "CPU-bound (main thread saturated)" } elseif ((& $avg 'gpuPct') -ge 90) { "GPU-bound" } elseif ((& $avg 'thread') -ge 85) { "both near the limit" } else { "neither saturated: pacing / stalls, not raw load" }
Write-Host "verdict: $bound"
