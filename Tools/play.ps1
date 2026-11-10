# Launches the Hawkeye game standalone (no editor UI) for fast iteration on feel.
# Refuses to start if an editor instance is already running - this machine has 16 GB RAM
# and editor + standalone game at once will thrash.
# Usage: .\Tools\play.ps1 [-Map /Game/Maps/L_District_EastVillage] [-Fullscreen] [-Notes]
#   -Map         play a different map instead of the district (L_District_EastVillage)
#   -Fullscreen  launch fullscreen instead of windowed at 1600x900
#   -Notes       wait for the game to close, then print the playtest session folder
#                (Saved\Playtest\<session>\: F12 notes, photos, summary.json, the log) and write
#                its report.md with Tools\playtest-report.py. See claude-docs\testing.md, "Playtest capture".
param(
    [string]$Map = "/Game/Maps/L_District_EastVillage",
    [switch]$Fullscreen,
    [switch]$Notes,
    [string]$Engine = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Proj = Join-Path $Root "Hawkeye.uproject"
$Exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"

if (-not (Test-Path $Exe)) { Write-Error "UnrealEditor.exe not found at $Exe"; exit 2 }
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    Write-Warning "An editor instance is already running. This machine has 16 GB RAM; not starting a second one."
    exit 1
}

# The session is the launch time; the game names its folder after it (-PlaytestSession).
$Session = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$ArgList = @("`"$Proj`"", $Map, "-game", "-PlaytestSession=$Session")
if ($Fullscreen) {
    $ArgList += @("-fullscreen", "-ResX=1920", "-ResY=1080", "-log")
} else {
    $ArgList += @("-windowed", "-ResX=1600", "-ResY=900", "-log")
}

$Game = Start-Process -FilePath $Exe -ArgumentList $ArgList -WorkingDirectory $Root -PassThru
Write-Host "Launching Hawkeye standalone on $Proj ($Map), playtest session $Session"
if (-not $Notes) { exit 0 }

Write-Host "F12 (or hold Menu on a pad) takes a note; F11 (or Pause > Photo mode) is photo mode. Waiting for the game to close..."
$Game.WaitForExit()
$Folder = Join-Path $Root "Saved\Playtest\$Session"
if (-not (Test-Path $Folder)) {
    Write-Warning "No session folder at $Folder (the game wrote nothing)."
    exit 1
}
$Report = Join-Path $Folder "report.md"
$Python = Get-Command python -ErrorAction SilentlyContinue
if ($Python) {
    & $Python.Source (Join-Path $Root "Tools\playtest-report.py") $Folder | Out-Null
}
Write-Host ""
Write-Host "Playtest session: $Folder"
if (Test-Path $Report) { Write-Host "Report:           $Report" }
exit 0
