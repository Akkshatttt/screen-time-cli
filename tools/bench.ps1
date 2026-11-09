# Resource bench: daemon steady state, event storm, CLI cost on synthetic DBs, binaries, vs StayFree.
# Usage: powershell -File tools/bench.ps1 [-Seconds 120] [-Quick]
param([int]$Seconds = 120, [switch]$Quick)
$ErrorActionPreference = 'Stop'
if ($Quick) { $Seconds = 30 }
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root 'build'
$data = Join-Path $bin 'bench-data'
$cli = Join-Path $bin 'screen-time-cli.exe'
$dmn = Join-Path $bin 'screen-time-daemon.exe'
$out = Join-Path $root 'bench\results'
New-Item -ItemType Directory -Force $out, $data | Out-Null
$report = New-Object System.Collections.Generic.List[string]
function Say($s) { $report.Add($s); Write-Host $s }

# budgets (today budget includes ~30 ms process start)
$BUD_RAM_MB = 3.0; $BUD_CPU = 0.05; $BUD_TODAY_MS = 80; $BUD_YEAR_MS = 500
$fail = @()

Get-Process screen-time-daemon -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item "$data\*" -Force -ErrorAction SilentlyContinue
$env:STC_DATA_DIR = $data

Say "# Bench $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
Say ""
Say "## Binaries"
foreach ($f in $cli, $dmn) { Say ("- {0}: {1:N0} KB" -f (Split-Path $f -Leaf), ((Get-Item $f).Length / 1KB)) }

function Snap($p) {
    $p.Refresh()
    $io = (Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)").WriteTransferCount
    [pscustomobject]@{ T = Get-Date; Cpu = $p.TotalProcessorTime.TotalSeconds; Ws = $p.WorkingSet64
                       Priv = $p.PrivateMemorySize64; H = $p.HandleCount; Th = $p.Threads.Count; W = $io }
}
function Summarize($label, $a, $b, $samples) {
    $sec = ($b.T - $a.T).TotalSeconds
    $cpu = ($b.Cpu - $a.Cpu) / $sec * 100
    $maxPriv = ($samples | Measure-Object Priv -Maximum).Maximum / 1MB
    Say ("- {0}: {1:N0}s, CPU {2:N3}%, private {3:N2} MB (max {4:N2}), working set {5:N2} MB, handles {6}->{7}, threads {8}, disk writes {9:N0} B/min" -f `
        $label, $sec, $cpu, ($b.Priv / 1MB), $maxPriv, ($b.Ws / 1MB), $a.H, $b.H, $b.Th, (($b.W - $a.W) / $sec * 60))
    return @{ Cpu = $cpu; Priv = $b.Priv / 1MB }
}

Say ""
Say "## Daemon"
$p = Start-Process $dmn -PassThru
Start-Sleep 3
$a = Snap $p; $samples = @($a)
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) { Start-Sleep 5; $samples += Snap $p }
$b = $samples[-1]
$s = Summarize "steady state" $a $b $samples
if ($s.Priv -gt $BUD_RAM_MB) { $fail += "daemon private bytes $([math]::Round($s.Priv,2)) MB > $BUD_RAM_MB" }
if ($s.Cpu -gt $BUD_CPU) { $fail += "daemon CPU $([math]::Round($s.Cpu,3))% > $BUD_CPU" }

# (no window-switch storm: launching windows is invasive; see FEATURES.md)
$spans = & python -I -c "import sqlite3;print(sqlite3.connect(r'$data\data.db').execute('select count(*) from span').fetchone()[0])"
$dbsz = (Get-ChildItem $data -Filter 'data.db*' | Measure-Object Length -Sum).Sum
Say ("- spans recorded: {0}, DB+WAL size {1:N0} KB" -f $spans, ($dbsz / 1KB))
& $cli stop | Out-Null; Start-Sleep 1

Say ""
Say "## CLI on synthetic data"
foreach ($y in 0.1, 1, 5) {
    $db = Join-Path $bin "synth_$y"; New-Item -ItemType Directory -Force $db | Out-Null
    Remove-Item "$db\*" -Force -ErrorAction SilentlyContinue
    Push-Location $root; $gen = & python -I tools/gen_db.py "$db\data.db" $y; Pop-Location
    $env:STC_DATA_DIR = $db
    foreach ($cmd in 'today', 'week', 'month') {
        $t = Measure-Command { & $cli $cmd | Out-Null }
        Say ("- {0}y data ({1}): {2} -> {3:N0} ms" -f $y, $gen, $cmd, $t.TotalMilliseconds)
        if ($cmd -eq 'today' -and $t.TotalMilliseconds -gt $BUD_TODAY_MS) { $fail += "today on ${y}y data $([math]::Round($t.TotalMilliseconds)) ms > $BUD_TODAY_MS" }
        if ($y -ge 1 -and $t.TotalMilliseconds -gt $BUD_YEAR_MS) { $fail += "$cmd on ${y}y data $([math]::Round($t.TotalMilliseconds)) ms > $BUD_YEAR_MS" }
    }
    Say ("  DB size {0:N1} MB" -f ((Get-Item "$db\data.db").Length / 1MB))
}

Say ""
Say "## Reference: StayFree now"
$sf = Get-Process StayFree -ErrorAction SilentlyContinue
if ($sf) { Say ("- {0} processes, {1:N0} MB private total" -f $sf.Count, (($sf | Measure-Object PrivateMemorySize64 -Sum).Sum / 1MB)) } else { Say "- not running" }

Say ""
Say "## Budgets"
if ($fail) { $fail | ForEach-Object { Say "- FAIL: $_" } } else { Say "- all within budget (daemon <= $BUD_RAM_MB MB private, < $BUD_CPU% CPU; CLI today < $BUD_TODAY_MS ms, 1y+ < $BUD_YEAR_MS ms)" }

$file = Join-Path $out ("{0}.md" -f (Get-Date -Format 'yyyy-MM-dd_HHmm'))
$report | Set-Content -Encoding utf8 $file
Write-Host "saved $file"
if ($fail) { exit 1 }
