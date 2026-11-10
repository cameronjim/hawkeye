# Measures how long the district takes to become playable in the standalone game (-game).
#
# Each run launches UnrealEditor-Cmd.exe -game on L_District_EastVillage with
# -HawkeyeQuitAfterPlayable, reads the game mode's "Playable after X s" line and the frame
# summary after it, and prints one row per run. The first run of a session is the cold one when
# nothing Unreal is running beforehand (no zenserver, no editor); the rest are warm.
#
#   .\Tools\measure-load.ps1                 # 3 runs, 25 s of frames after the playable mark
#   .\Tools\measure-load.ps1 -Runs 1 -WatchSeconds 5
#
# Do not run this while another editor instance is open.

[CmdletBinding()]
param(
    [int]$Runs = 3,
    [double]$WatchSeconds = 25,
    [string]$Engine = "",
    [string]$Tag = "load",
    # Seconds after the first playable mark to reopen the level once (a death with no save does
    # that); the reload's own playable time is reported too. Negative: no reload.
    [double]$ReloadAfter = -1
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Proj = Join-Path $Root "Hawkeye.uproject"
$Cmd = Get-HawkeyeEditorCmd -EngineRoot $Engine
if (-not (Test-Path $Cmd)) { throw "UnrealEditor-Cmd.exe not found at $Cmd" }

$rows = @()
for ($i = 1; $i -le $Runs; $i++) {
    $zen = @(Get-Process zenserver -ErrorAction SilentlyContinue).Count -gt 0
    $log = Join-Path $Root "Saved\Logs\Measure_${Tag}_$i.log"
    if (Test-Path $log) { Remove-Item $log -Force }
    $start = Get-Date
    & $Cmd $Proj /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log `
        "-abslog=$log" "-HawkeyeQuitAfterPlayable=$WatchSeconds" "-HawkeyeReloadAfterPlayable=$ReloadAfter" | Out-Null
    $wall = ((Get-Date) - $start).TotalSeconds
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { "" }
    $playable = [regex]::Match($text, "Playable after ([0-9.]+) s \(process start")
    $reload = [regex]::Match($text, "Playable after ([0-9.]+) s \(level change")
    $loadMap = [regex]::Match($text, "Took ([0-9.]+) seconds to LoadMap\(/Game/Maps/L_District_EastVillage\)")
    $summary = [regex]::Match($text, "First [0-9]+ s after the playable mark: ([^\r\n]+)")
    $row = [pscustomobject]@{
        Run = $i
        ZenAlreadyUp = $zen
        PlayableS = if ($playable.Success) { [double]$playable.Groups[1].Value } else { $null }
        LoadMapS = if ($loadMap.Success) { [double]$loadMap.Groups[1].Value } else { $null }
        ReloadS = if ($reload.Success) { [double]$reload.Groups[1].Value } else { $null }
        ProcessS = [math]::Round($wall, 1)
        Frames = if ($summary.Success) { $summary.Groups[1].Value } else { "(no summary)" }
    }
    $rows += $row
    Write-Host ("run {0}: playable {1} s, LoadMap {2} s, reload {3} s, zen already up {4}; {5}" -f $row.Run, $row.PlayableS, $row.LoadMapS, $row.ReloadS, $row.ZenAlreadyUp, $row.Frames)
}
$rows | Format-Table -AutoSize | Out-String -Width 400 | Write-Host
