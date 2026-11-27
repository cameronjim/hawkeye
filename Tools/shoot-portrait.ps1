# Takes one portfolio still of Kate, Clint and Lucky (Hawkeye.Screenshot.Portrait) into Saved\Screenshots\Portrait\portrait_<preset>.png.
#
#   .\Tools\shoot-portrait.ps1 -Preset a                 # a: the social club's roof, b: the grotto's frontage
#   .\Tools\shoot-portrait.ps1 -Preset a -Extra "-HawkeyePortraitDist=600","-HawkeyePortraitYaw=90"   # scouting overrides
#
# One standalone game (-game, a real renderer, windowed) per shot, 8 minute timeout (a stalled launch is killed and
# reported). Refuses to start while any Unreal or Blender process is up: this machine has 16 GB of RAM, and an Unreal
# you did not start means the owner is playing. -unattended makes the process use the automation save slots, so the
# campaign and settings saves are not touched. The build is a separate step (Tools\gate.ps1 or Build.bat).
[CmdletBinding()]
param(
    [string]$Preset = "a",
    [string[]]$Extra = @(),
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [int]$TimeoutMinutes = 8,
    [string]$Engine = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Root = Split-Path -Parent $PSScriptRoot
$engineRoot = Resolve-HawkeyeEngine -Explicit $Engine
$cmd = Get-HawkeyeEditorCmd -EngineRoot $engineRoot

$busy = (& tasklist /NH 2>$null) -join "`n"
if ($busy -match "(?im)^(UnrealEditor|blender)[^\s]*\.exe\s") {
    Write-Warning "An Unreal or Blender process is already running; not starting another."
    exit 1
}

# The chapter that dresses the place: the roof is the social club's, the frontage the grotto's.
$chapter = switch ($Preset) {
    "a" { "DA_CH04_SocialClub" }
    "b" { "DA_CH03_Grotto" }
    default { "" }
}
$outDir = Join-Path $Root "Saved\Screenshots\Portrait"
New-Item -ItemType Directory -Force $outDir | Out-Null
$log = Join-Path $Root "Saved\Logs\Portrait_$Preset.log"
$proj = Join-Path $Root "Hawkeye.uproject"
$argList = @("`"$proj`"", "/Game/Maps/L_District_EastVillage", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-NoMainMenu",
    "-HawkeyePortrait=$Preset", "-unattended", "-nosplash", "-log", "-abslog=`"$log`"",
    "-ExecCmds=`"Automation RunTests Hawkeye.Screenshot.Portrait; Quit`"")
if ($chapter) { $argList += "-HawkeyeChapter=$chapter" }
if ($Extra.Count -gt 0) { $argList += $Extra }

Write-Host "[portrait $Preset] chapter '$chapter', ${ResX}x${ResY}, timeout $TimeoutMinutes min ..."
$proc = Start-Process -FilePath $cmd -ArgumentList $argList -PassThru
if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Write-Warning "still running after $TimeoutMinutes min: killing process $($proc.Id) and its children."
    & taskkill /PID $proc.Id /T /F | Out-Null
    exit 1
}
Select-String -Path $log -Pattern "Portrait|Test Completed|Error:" | Select-Object -First 25 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
Write-Host "Wrote $outDir\portrait_$Preset.png (exit $($proc.ExitCode))"
