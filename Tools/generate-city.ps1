# Builds /Game/Maps/L_District_EastVillage from OpenStreetMap data.
#
#   1. Tools\fetch-osm.ps1 if Tools\Data\osm\east_village.buildings.json is missing
#   2. Tools\Editor\generate_city.py in a headless editor (idempotent: a second run with the
#      same data rebuilds nothing and does not save the map)
#   3. with -Verify, Tools\Editor\verify_city.py afterwards
#
# Do not run this while another editor instance is open.
#
#   .\Tools\generate-city.ps1
#   .\Tools\generate-city.ps1 -Verify

[CmdletBinding()]
param(
    [string]$Engine = "",
    [switch]$Verify
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$Project = Join-Path $ProjectRoot "Hawkeye.uproject"
$EditorCmd = Get-HawkeyeEditorCmd -EngineRoot $Engine
$Records = Join-Path $ScriptDir "Data\osm\east_village.buildings.json"
$LogDir = Join-Path $ProjectRoot "Saved\Logs"

if (-not (Test-Path $EditorCmd)) { throw "Headless editor not found: $EditorCmd" }
if (-not (Test-Path $LogDir))    { New-Item -ItemType Directory -Path $LogDir | Out-Null }

if (-not (Test-Path $Records)) {
    Write-Host "No OSM records yet; fetching."
    & (Join-Path $ScriptDir "fetch-osm.ps1")
    if ($LASTEXITCODE -ne 0) { throw "fetch-osm.ps1 failed" }
}

function Invoke-EditorPython([string]$ScriptName, [string]$LogName) {
    $script = Join-Path $ScriptDir "Editor\$ScriptName"
    $log = Join-Path $LogDir $LogName
    if (Test-Path $log) { Remove-Item $log -Force }
    Write-Host "Running $ScriptName ..."
    & $EditorCmd $Project -run=pythonscript "-script=$script" -unattended -nullrhi -nosplash -nop4 `
        -stdout -FullStdOutLogOutput -NoLogTimes "-abslog=$log" | Out-Null
    $exit = $LASTEXITCODE
    if (-not (Test-Path $log)) { throw "No log at $log" }
    $lines = Select-String -Path $log -Pattern "LogPython" | ForEach-Object { $_.Line }
    # One line per piece is too much to print for 600 buildings; show the summary lines.
    $lines | Where-Object { $_ -notmatch "\[Hawkeye\] (exists|created|updated)\s+/Game/City/" } |
        ForEach-Object { Write-Host $_ }
    $bad = $lines | Where-Object { $_ -match "Error|Traceback|FAILED|\] FAIL " }
    if ($bad) {
        Write-Host "$ScriptName reported errors (full log: $log)" -ForegroundColor Red
        return 1
    }
    if ($exit -ne 0) {
        Write-Host "Editor exited with code $exit (full log: $log)" -ForegroundColor Red
        return $exit
    }
    return 0
}

$result = Invoke-EditorPython "generate_city.py" "HawkeyeCity.log"
if ($result -ne 0) { exit $result }

if ($Verify) {
    $result = Invoke-EditorPython "verify_city.py" "HawkeyeCityVerify.log"
    if ($result -ne 0) { exit $result }
}

Write-Host "City generation finished clean." -ForegroundColor Green
exit 0
