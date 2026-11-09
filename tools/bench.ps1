# Resource bench: daemon steady state, CLI cost on synthetic DBs, binaries, vs StayFree.
# Usage: powershell -File tools/bench.ps1 [-Seconds 120] [-Quick] [-Own] [-Profile] [-NoCli]
#   default  measures the installed daemon in place if one is running (never stops it), else starts its own
#   -Own     starts a separate sandboxed daemon (own data folder and kernel object names) even if one is running,
#            so a freshly built exe can be measured without touching the installed one
#   -Profile like -Own, with the build/screen-time-daemon-profile.exe that counts events and time per minute
param([int]$Seconds = 120, [switch]$Quick, [switch]$Own, [switch]$Profile, [switch]$NoCli)
$ErrorActionPreference = 'Stop'
if ($Quick) { $Seconds = 30 }
if ($Profile) { $Own = $true }
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root 'build'
$data = Join-Path $bin 'bench-data'
$cli = Join-Path $bin 'screen-time-cli.exe'
$dmn = Join-Path $bin ($(if ($Profile) { 'screen-time-daemon-profile.exe' } else { 'screen-time-daemon.exe' }))
$out = Join-Path $root 'bench\results'
New-Item -ItemType Directory -Force $out, $data | Out-Null
$report = New-Object System.Collections.Generic.List[string]
function Say($s) { $report.Add($s); Write-Host $s }

# budgets (today budget includes ~30 ms process start)
# private bytes: an empty 32-bit exe on 64-bit Windows already commits 0.81 MB (WoW64), so the budget sits above that
$BUD = @{ WsMB = 3.0; PrivWsMB = 0.6; PrivMB = 1.5; Cpu = 0.05; WritesKBperChange = 24; TodayMs = 80; YearMs = 500 }
$fail = @()

$live = Get-Process screen-time-daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -notlike "$bin*" } | Select-Object -First 1
Get-Process screen-time-daemon, screen-time-daemon-profile -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$bin*" } | Stop-Process -Force
Remove-Item "$data\*" -Force -ErrorAction SilentlyContinue

Say "# Bench $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
Say ""
Say "## Binaries"
foreach ($f in $cli, $dmn) { Say ("- {0}: {1:N0} KB" -f (Split-Path $f -Leaf), ((Get-Item $f).Length / 1KB)) }

function Snap($p) {
    $p.Refresh()
    $cim = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($p.Id)"
    $io = (Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)").WriteTransferCount
    [pscustomobject]@{ T = Get-Date; Cpu = $p.TotalProcessorTime.TotalSeconds; Ws = $p.WorkingSet64
                       Priv = $p.PrivateMemorySize64; PrivWs = $cim.WorkingSetPrivate; H = $p.HandleCount
                       Th = $p.Threads.Count; W = $io; Mods = $p.Modules.Count }
}

Say ""
Say "## Daemon"
$useLive = $live -and -not $Own
if ($useLive) {
    $p = $live
    Say "- measuring the running installed daemon (pid $($p.Id)), not restarting it"
    $dataDir = Join-Path $env:LOCALAPPDATA 'screen-time-cli'
} else {
    $env:STC_DATA_DIR = $data
    $env:STC_NS = 'bench'                 # separate mutex and events: runs beside the installed daemon
    $p = Start-Process $dmn -PassThru
    Say "- started sandboxed $(Split-Path $dmn -Leaf) (pid $($p.Id)); the installed daemon, if any, keeps running"
    Start-Sleep 3
    $dataDir = $data
}
$a = Snap $p; $samples = @($a)
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) { Start-Sleep 5; $samples += Snap $p }
$b = $samples[-1]
$sec = ($b.T - $a.T).TotalSeconds
$cpu = ($b.Cpu - $a.Cpu) / $sec * 100
$wkb = ($b.W - $a.W) / $sec * 60 / 1KB
$wsMB = $b.Ws / 1MB; $privMB = $b.Priv / 1MB; $privWsMB = $b.PrivWs / 1MB
Say ("- steady state over {0:N0}s: CPU {1:N3}%, disk writes {2:N1} KB/min, handles {3}->{4}, threads {5}, modules {6}" -f $sec, $cpu, $wkb, $a.H, $b.H, $b.Th, $b.Mods)
Say "  (disk writes scale with how often you switch windows: each change costs a few KB; see spans recorded below)"
Say ("- memory: working set {0:N2} MB, private working set {1:N2} MB, private bytes {2:N2} MB (max {3:N2})" -f $wsMB, $privWsMB, $privMB, (($samples | Measure-Object Priv -Maximum).Maximum / 1MB))
if ($wsMB -gt $BUD.WsMB) { $fail += "working set $([math]::Round($wsMB,2)) MB > $($BUD.WsMB)" }
if ($privWsMB -gt $BUD.PrivWsMB) { $fail += "private working set $([math]::Round($privWsMB,2)) MB > $($BUD.PrivWsMB)" }
if ($privMB -gt $BUD.PrivMB) { $fail += "private bytes $([math]::Round($privMB,2)) MB > $($BUD.PrivMB)" }
if ($cpu -gt $BUD.Cpu) { $fail += "daemon CPU $([math]::Round($cpu,3))% > $($BUD.Cpu)" }

$db = "$dataDir\data.db"
if (Test-Path $db) {
    $spans = & python -I -c "import sqlite3;c=sqlite3.connect(r'file:${db}?mode=ro', uri=True);print(c.execute('select count(*) from span').fetchone()[0])"
    $dbsz = (Get-ChildItem $dataDir -Filter 'data.db*' | Measure-Object Length -Sum).Sum
    $wal = (Get-Item "$db-wal" -ErrorAction SilentlyContinue).Length
    Say ("- spans recorded: {0}, DB+WAL size {1:N0} KB (WAL {2:N0} KB)" -f $spans, ($dbsz / 1KB), ($wal / 1KB))
    $changes = [math]::Max(1, [int]$spans - 1)
    $perChange = $wkb * ($sec / 60) / $changes
    Say ("- disk writes: {0:N1} KB per recorded span change (heartbeats cost ~0: 24 bytes into hb.bin)" -f $perChange)
    if ($perChange -gt $BUD.WritesKBperChange) { $fail += "disk writes $([math]::Round($perChange,1)) KB per span change > $($BUD.WritesKBperChange)" }
}
if ($Profile) {
    $env:STC_BENCH_DB = $db
    $prof = @'
import os, sqlite3
c = sqlite3.connect("file:" + os.environ["STC_BENCH_DB"] + "?mode=ro", uri=True)
r = c.execute("select value from meta where key='profile'").fetchone()
print(r[0] if r else "none")
'@ | python -I -
    Say "- profile counters: $prof"
    $mins = ($sec + 3) / 60
    if ($prof -match 'fg=(\d+) title=(\d+) raw=(\d+).*evals=(\d+).*probes=(\d+) eval_us=(\d+) probe_us=(\d+) cycles=(\d+) startup_cycles=(\d+)') {
        Say ("  per minute: foreground changes {0:N1}, name-change callbacks {1:N1} (fg window title {2:N1}), evals {3:N1}, probes {4:N2}; time in eval {5:N0} us/min, in probes {6:N0} us/min" -f ($matches[1]/$mins), ($matches[3]/$mins), ($matches[2]/$mins), ($matches[4]/$mins), ($matches[5]/$mins), ($matches[6]/$mins), ($matches[7]/$mins))
        $mhz = (Get-CimInstance Win32_Processor | Select-Object -First 1).MaxClockSpeed
        $steady = [double]$matches[8] - [double]$matches[9]
        $cycCpu = $steady / ($mhz * 1e6) / ($sec + 3) * 100
        Say ("  startup cost: {0:N0} M cycles once. Steady state from the cycle counter ({1} MHz assumed): {2:N4}% CPU, {3:N1} M cycles/min" -f ([double]$matches[9] / 1e6), $mhz, $cycCpu, ($steady / 1e6 / $mins))
    }
}
if (-not $useLive) {
    Say "- modules loaded:"
    ($p.Modules | Select-Object -ExpandProperty ModuleName | Sort-Object -Unique) -join ', ' | ForEach-Object { Say "  $_" }
    & $cli stop | Out-Null; Start-Sleep 1
    Remove-Item Env:STC_NS -ErrorAction SilentlyContinue
}

if (-not $NoCli) {
    Say ""
    Say "## CLI on synthetic data"
    foreach ($y in 0.1, 1, 5) {
        $sdb = Join-Path $bin "synth_$y"; New-Item -ItemType Directory -Force $sdb | Out-Null
        Remove-Item "$sdb\*" -Force -ErrorAction SilentlyContinue
        Push-Location $root; $gen = & python -I tools/gen_db.py "$sdb\data.db" $y; Pop-Location
        $env:STC_DATA_DIR = $sdb
        foreach ($cmd in 'today', 'week', 'month', 'heatmap', 'trends', 'focus', 'switches', 'topics') {
            & $cli $cmd | Out-Null                          # warm file cache; first launch is noisy
            $t = Measure-Command { & $cli $cmd | Out-Null }
            Say ("- {0}y data ({1}): {2} -> {3:N0} ms" -f $y, $gen, $cmd, $t.TotalMilliseconds)
            if ($cmd -eq 'today' -and $t.TotalMilliseconds -gt $BUD.TodayMs) { $fail += "today on ${y}y data $([math]::Round($t.TotalMilliseconds)) ms > $($BUD.TodayMs)" }
            if ($y -ge 1 -and $t.TotalMilliseconds -gt $BUD.YearMs) { $fail += "$cmd on ${y}y data $([math]::Round($t.TotalMilliseconds)) ms > $($BUD.YearMs)" }
        }
        Say ("  DB size {0:N1} MB" -f ((Get-Item "$sdb\data.db").Length / 1MB))
    }
}

Say ""
Say "## Reference: StayFree now"
$sf = Get-Process StayFree -ErrorAction SilentlyContinue
if ($sf) { Say ("- {0} processes, {1:N0} MB private total" -f $sf.Count, (($sf | Measure-Object PrivateMemorySize64 -Sum).Sum / 1MB)) } else { Say "- not running" }

Say ""
Say "## Budgets"
if ($fail) { $fail | ForEach-Object { Say "- FAIL: $_" } }
else { Say "- all within budget (working set <= $($BUD.WsMB) MB, private working set <= $($BUD.PrivWsMB) MB, private bytes <= $($BUD.PrivMB) MB, CPU < $($BUD.Cpu)%, writes <= $($BUD.WritesKBperChange) KB per span change, CLI today < $($BUD.TodayMs) ms)" }

$file = Join-Path $out ("{0}.md" -f (Get-Date -Format 'yyyy-MM-dd_HHmm'))
$report | Set-Content -Encoding utf8 $file
Write-Host "saved $file"
if ($fail) { exit 1 }
