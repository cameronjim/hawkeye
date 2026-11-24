# Builds HawkeyeEditor and takes the Hawkeye.Screenshot.Bodies shots (a real RHI, windowed): the full shots and the hand
# close-ups in Saved\Screenshots\Bodies. Refuses to run while another Unreal or Blender is open (16 GB of RAM).
#   .\Tools\shoot-bodies.ps1 [-NoBuild] [-Log <path>]
param(
    [switch]$NoBuild,
    [string]$Log = ""
)

. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit ""
$Root = Split-Path -Parent $PSScriptRoot
$Proj = Join-Path $Root "Hawkeye.uproject"
if (-not $Log) { $Log = Join-Path $Root "Saved\Logs\Bodies.log" }

if (Get-Process -Name "UnrealEditor*", "blender" -ErrorAction SilentlyContinue) { Write-Error "Unreal or Blender is already running."; exit 2 }
if (-not $NoBuild) {
    & (Join-Path $Engine "Engine\Build\BatchFiles\Build.bat") HawkeyeEditor Win64 Development -project="$Proj" -waitmutex | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit 1 }
}
& (Get-HawkeyeEditorCmd -EngineRoot $Engine) $Proj /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720 -TimeOfDay=Day -unattended -nosplash -log -abslog="$Log" -ExecCmds="Automation RunTests Hawkeye.Screenshot.Bodies; Quit" | Out-Null
Select-String -Path $Log -Pattern "Test Completed|Error:" | Select-Object -First 10 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
