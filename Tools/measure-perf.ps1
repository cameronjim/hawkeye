# Frame time, the stat unit split and memory through the scripted runs, in the standalone game (-game).
#
# Launches UnrealEditor-Cmd.exe -game on the district with -HawkeyePerfLog (the game mode logs a "Perf [...]"
# line every -Window seconds: frames, avg, p95, worst, game/draw/GPU ms, frames over 50 ms, working set and
# peak) and -HawkeyeHitchMs=50 (every frame over 50 ms after the playable mark is logged as a hitch), runs the
# tests, samples the process's own working set once a second, and prints one row per test phase.
#
#   .\Tools\measure-perf.ps1                                  # the laps, the tour, fast travel, the interior walk, night
#   .\Tools\measure-perf.ps1 -TimeOfDay Day -Tag day
#   .\Tools\measure-perf.ps1 -ResX 1920 -ResY 1080 -Tests "Hawkeye.Lap.StreetFight" -Tag fight1080
#   .\Tools\measure-perf.ps1 -StatDump 20 -Tag dump          # "stat dumpave" every 20 s and "stat dumphitches"
#
# Only this process is sampled (by its id), never another editor. Do not run it while another engine runs.

[CmdletBinding()]
param(
    [string]$Tests = "Hawkeye.Lap.EastVillage+Hawkeye.Lap.StreetFight+Hawkeye.Lap.ArcherDuel+Hawkeye.Perf.Tour+Hawkeye.Lap.FastTravel+Hawkeye.Lap.InteriorWalk",
    [string]$TimeOfDay = "",
    [int]$ResX = 1280,
    [int]$ResY = 720,
    [double]$Window = 2,
    [double]$StatDump = -1,
    [string]$Tag = "perf",
    # Extra command-line switches for a diagnostic run (e.g. "-dpcvars=r.SkyLight.RealTimeReflectionCapture=0").
    [string]$Extra = "",
    [string]$Engine = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Proj = Join-Path $Root "Hawkeye.uproject"
$Cmd = Get-HawkeyeEditorCmd -EngineRoot $Engine
$Out = Join-Path $Root "Saved\Perf"
New-Item -ItemType Directory -Force $Out | Out-Null
$Log = Join-Path $Out "$Tag.log"
$Samples = Join-Path $Out "$Tag.memory.csv"
$Report = Join-Path $Out "$Tag.report"
foreach ($f in @($Log, $Samples)) { if (Test-Path $f) { Remove-Item $f -Force } }

$argList = @("`"$Proj`"", "/Game/Maps/L_District_EastVillage", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-unattended",
    "-nosplash", "-log", "-abslog=`"$Log`"", "-HawkeyePerfLog=$Window", "-HawkeyeHitchMs=50", "-HawkeyeStatDump=$StatDump",
    "-ReportExportPath=`"$Report`"", "-ExecCmds=`"Automation RunTests $Tests; Quit`"")
if ($TimeOfDay) { $argList += "-TimeOfDay=$TimeOfDay" }
if ($Extra) { $argList += $Extra }
$start = Get-Date
$proc = Start-Process -FilePath $Cmd -ArgumentList $argList -PassThru
"seconds,working_set_gb,private_gb" | Out-File $Samples -Encoding utf8
$peak = 0.0
while (-not $proc.HasExited) {
    try {
        $proc.Refresh()
        $ws = $proc.WorkingSet64 / 1GB
        $pv = $proc.PrivateMemorySize64 / 1GB
        $peak = [math]::Max($peak, $ws)
        ("{0:F0},{1:F3},{2:F3}" -f ((Get-Date) - $start).TotalSeconds, $ws, $pv) | Out-File $Samples -Append -Encoding utf8
    } catch { }
    Start-Sleep -Seconds 1
}
$wall = ((Get-Date) - $start).TotalSeconds

$text = if (Test-Path $Log) { Get-Content $Log -Raw } else { "" }
$rows = @{}
$order = @()
foreach ($m in [regex]::Matches($text, "Perf \[([^\]]+)\] \S+ [0-9.]+-[0-9.]+ s: (\d+) frames, avg ([0-9.]+) ms, p95 ([0-9.]+), worst ([0-9.]+), game ([0-9.]+), draw ([0-9.]+), gpu ([0-9.]+), (\d+) over 50 ms, (\d+) captures; working set ([0-9.]+) GB, peak ([0-9.]+) GB")) {
    $label = $m.Groups[1].Value
    if (-not $rows.ContainsKey($label)) { $rows[$label] = @(); $order += $label }
    $rows[$label] += , $m
}
$table = foreach ($label in $order) {
    $ms = $rows[$label]
    $frames = ($ms | % { [int]$_.Groups[2].Value } | Measure-Object -Sum).Sum
    $w = { param($g) (($ms | % { [double]$_.Groups[$g].Value * [int]$_.Groups[2].Value } | Measure-Object -Sum).Sum) / [math]::Max($frames, 1) }
    [pscustomobject]@{
        Phase = $label
        Frames = $frames
        AvgMs = [math]::Round((& $w 3), 1)
        P95Ms = [math]::Round((($ms | % { [double]$_.Groups[4].Value }) | Measure-Object -Maximum).Maximum, 1)
        WorstMs = [math]::Round((($ms | % { [double]$_.Groups[5].Value }) | Measure-Object -Maximum).Maximum, 1)
        GameMs = [math]::Round((& $w 6), 1)
        DrawMs = [math]::Round((& $w 7), 1)
        GpuMs = [math]::Round((& $w 8), 1)
        Over50 = ($ms | % { [int]$_.Groups[9].Value } | Measure-Object -Sum).Sum
        WsGB = [math]::Round((($ms | % { [double]$_.Groups[11].Value }) | Measure-Object -Maximum).Maximum, 2)
    }
}
$table | Format-Table -AutoSize | Out-String -Width 300 | Write-Host
$playable = [regex]::Matches($text, "Playable after ([0-9.]+) s \(([a-z ]+) to") | % { "$($_.Groups[1].Value) s ($($_.Groups[2].Value))" }
$hitches = [regex]::Matches($text, "Hitch: a (\d+) ms frame ([0-9.]+) s after the playable mark[^\r\n]*") | % { $_.Value }
$results = [regex]::Matches($text, "Test Completed\. Result=\{(\w+)\}[^\r\n]*Path=\{([^}]+)\}") | % { "$($_.Groups[2].Value): $($_.Groups[1].Value)" }
Write-Host "Playable marks: $($playable -join ', ')"
Write-Host "Peak working set (sampled, this process only): $([math]::Round($peak, 2)) GB; engine's own peak: $(([regex]::Matches($text, 'peak ([0-9.]+) GB') | % { [double]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum) GB"
Write-Host "Hitches over 50 ms after a playable mark: $($hitches.Count)"
$hitches | Select-Object -First 40 | % { Write-Host "  $_" }
Write-Host "Tests: $($results -join '; ')"
Write-Host "Wall $([math]::Round($wall)) s. Log $Log"
