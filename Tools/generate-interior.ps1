# Builds every interior map (/Game/Maps/L_Int_<Name>) from its layout in Tools\Interiors\<Name>.json.
#
#   1. Tools\Editor\generate_interior.py in a headless editor (idempotent: a second run with the same
#      layout changes nothing and does not save the map)
#   2. with -Verify, Tools\Editor\verify_interiors.py afterwards
#
# The layout rules have their own tests with no editor: Tools\test-interior.ps1.
# Do not run this while another editor instance is open.
#
#   .\Tools\generate-interior.ps1
#   .\Tools\generate-interior.ps1 -Interior Sample -Verify

[CmdletBinding()]
param(
    [string]$Engine = "",
    [string]$Interior = "",
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
$LogDir = Join-Path $ProjectRoot "Saved\Logs"

if (-not (Test-Path $EditorCmd)) { throw "Headless editor not found: $EditorCmd" }
if (-not (Test-Path $LogDir))    { New-Item -ItemType Directory -Path $LogDir | Out-Null }

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
    $lines | Where-Object { $_ -notmatch "\[Hawkeye\] (exists)\s+/Game/Materials/" } | ForEach-Object { Write-Host $_ }
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

if ($Interior) { $env:HAWKEYE_INTERIOR = $Interior }
try {
    $result = Invoke-EditorPython "generate_interior.py" "HawkeyeInterior.log"
    if ($result -ne 0) { exit $result }
    if ($Verify) {
        $result = Invoke-EditorPython "verify_interiors.py" "HawkeyeInteriorVerify.log"
        if ($result -ne 0) { exit $result }
    }
} finally {
    if ($Interior) { Remove-Item Env:\HAWKEYE_INTERIOR -ErrorAction SilentlyContinue }
}

Write-Host "Interior generation finished clean." -ForegroundColor Green
exit 0
