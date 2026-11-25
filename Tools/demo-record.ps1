# Starts the game for one of the owner's three demo clips and puts the campaign save back afterwards.
#
#   .\Tools\demo-record.ps1 -Clip street_fight       # chapter 1's district, the street group on Avenue A
#   .\Tools\demo-record.ps1 -Clip stealth_takedown   # chapter 3 (the Grotto) from its start
#   .\Tools\demo-record.ps1 -Clip tomas_phase        # chapter 4 (the social club) from its start
#   .\Tools\demo-record.ps1 -Restore <folder>        # put a backup folder's saves back by hand
#
# Playing the game, as opposed to running a test, writes your own campaign slot: the roaming autosave and every
# objective save to Saved\SaveGames\HawkeyeCampaign.sav, and the settings to HawkeyeSettings.sav (automation runs and
# the demo capture use their own slots, a clip recorded by hand does not). So this script copies both files to
# Saved\Demo\save-backup\<time>\ before it starts the game, waits for the game to close, and copies them back. If
# there was no campaign save before, the one the clip made is removed. Your campaign is as it was.
#
# -NoMainMenu skips the boot menu, so Continue cannot load your save into the clip's chapter. -HawkeyeChapter=<asset>
# starts that chapter's mission in the district (the asset under /Game/Missions; the laps use the same names).
# Fullscreen 1920x1080 like play.ps1 -Fullscreen. Close the game with Alt+F4 or Esc, Quit, when the clip is done.
[CmdletBinding(DefaultParameterSetName = "Clip")]
param(
    [Parameter(ParameterSetName = "Clip")]
    [ValidateSet("street_fight", "stealth_takedown", "tomas_phase")]
    [string]$Clip = "",
    [Parameter(ParameterSetName = "Restore")]
    [string]$Restore = "",
    [switch]$Windowed,
    [string]$Engine = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Saves = Join-Path $Root "Saved\SaveGames"
$Slots = @("HawkeyeCampaign.sav", "HawkeyeSettings.sav")

function Restore-Saves {
    param([string]$From)
    foreach ($name in $Slots) {
        $backup = Join-Path $From $name
        $live = Join-Path $Saves $name
        if (Test-Path $backup) {
            Copy-Item $backup $live -Force
            Write-Host "restored $name"
        } elseif (Test-Path $live) {
            Remove-Item $live -Force
            Write-Host "removed $name (there was none before)"
        }
    }
}

if ($PSCmdlet.ParameterSetName -eq "Restore") {
    if (-not (Test-Path $Restore)) { Write-Error "No such folder: $Restore"; exit 2 }
    Restore-Saves -From $Restore
    exit 0
}
if (-not $Clip) { Write-Error "Give -Clip street_fight, stealth_takedown or tomas_phase."; exit 2 }

$engineRoot = Resolve-HawkeyeEngine -Explicit $Engine
$exe = Join-Path $engineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
if (-not (Test-Path $exe)) { Write-Error "UnrealEditor.exe not found at $exe"; exit 2 }
if ((& tasklist /NH 2>$null) -match "(?im)^UnrealEditor(-Cmd)?\.exe\s") {
    Write-Warning "An Unreal process is already running. This machine has 16 GB RAM; close it first."
    exit 1
}

$backup = Join-Path $Root ("Saved\Demo\save-backup\" + (Get-Date -Format "yyyy-MM-dd_HH-mm-ss"))
New-Item -ItemType Directory -Force $backup | Out-Null
New-Item -ItemType Directory -Force $Saves | Out-Null
foreach ($name in $Slots) {
    $live = Join-Path $Saves $name
    if (Test-Path $live) { Copy-Item $live (Join-Path $backup $name) -Force }
}
Write-Host "Saves backed up to $backup"

$chapter = switch ($Clip) {
    "street_fight" { "" }
    "stealth_takedown" { "-HawkeyeChapter=DA_CH03_Grotto" }
    "tomas_phase" { "-HawkeyeChapter=DA_CH04_SocialClub" }
}
$argList = @("`"$(Join-Path $Root 'Hawkeye.uproject')`"", "/Game/Maps/L_District_EastVillage", "-game", "-NoMainMenu", "-Difficulty=Normal", "-log")
if ($chapter) { $argList += $chapter }
if ($Windowed) { $argList += @("-windowed", "-ResX=1920", "-ResY=1080") } else { $argList += @("-fullscreen", "-ResX=1920", "-ResY=1080") }

Write-Host "Starting the game for $Clip. Record with your overlay; close the game when you have the take."
$game = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $Root -PassThru
$game.WaitForExit()
Restore-Saves -From $backup
Write-Host "Done. Your campaign save is back as it was (backup kept in $backup)."
exit 0
