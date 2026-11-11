# Creates the starter content (input assets, the GASP import, blueprints, materials, weapon and
# chapter data assets, and the East Village district) by running Tools\Editor\create_all.py
# inside a headless editor.
#
# Idempotent: every script checks for the asset before creating it, so re-running is safe.
# Do not run this while another editor instance is open - they collide on the module DLL.
#
#   .\Tools\create-content.ps1
#   .\Tools\create-content.ps1 -Bright   # brighter interior exposure, for playtesting layout and AI

[CmdletBinding()]
param(
    [string]$Engine = "",
    [string]$Project = "",
    [switch]$Bright
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"

# $PSScriptRoot is empty inside a param() default when invoked via -File, so resolve here.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")
if (-not $Project) { $Project = Join-Path $ProjectRoot "Hawkeye.uproject" }
$Project = (Resolve-Path $Project).Path
$EditorCmd = Get-HawkeyeEditorCmd -EngineRoot $Engine
$Script = Join-Path $ProjectRoot "Tools\Editor\create_all.py"
$LogDir = Join-Path $ProjectRoot "Saved\Logs"
$LogFile = Join-Path $LogDir "HawkeyeContent.log"

if (-not (Test-Path $EditorCmd)) { throw "Headless editor not found: $EditorCmd" }
if (-not (Test-Path $Script))    { throw "Python entry point not found: $Script" }
if (-not (Test-Path $LogDir))    { New-Item -ItemType Directory -Path $LogDir | Out-Null }
if (Test-Path $LogFile)          { Remove-Item $LogFile -Force }

Write-Host "Running $Script through $EditorCmd ..."
if ($Bright) { Write-Host "Bright preset requested (HAWKEYE_BRIGHT=1)." -ForegroundColor Yellow }

# The editor-side importer (Tools\Editor\import_gasp.py) reads these from its environment.
$GaspRoot = Resolve-HawkeyeGaspRoot
$env:HAWKEYE_GASP_ROOT = $GaspRoot
$env:HAWKEYE_GASP_CONTENT = Join-Path $GaspRoot "Content"
# The Quaternius packs Tools\Editor\import_bodies.py imports the thugs', Clint's and Lucky's bodies from.
$env:HAWKEYE_ASSETS_ROOT = Resolve-HawkeyeAssetsRoot

if ($Bright) { $env:HAWKEYE_BRIGHT = "1" }
try {
    & $EditorCmd $Project `
        -run=pythonscript `
        "-script=$Script" `
        -unattended `
        -nullrhi `
        -nosplash `
        -nop4 `
        -stdout `
        -FullStdOutLogOutput `
        -NoLogTimes `
        "-abslog=$LogFile"

    $editorExit = $LASTEXITCODE
} finally {
    if ($Bright) { Remove-Item Env:\HAWKEYE_BRIGHT -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $LogFile)) {
    Write-Host "No log at $LogFile - the editor produced nothing to inspect." -ForegroundColor Red
    exit 1
}

$pythonLines = Select-String -Path $LogFile -Pattern "LogPython" | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "---- LogPython ----"
if ($pythonLines) {
    $pythonLines | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "(no LogPython output)"
}
Write-Host "-------------------"

$bad = $pythonLines | Where-Object { $_ -match "Error|Traceback" }

if ($bad) {
    Write-Host ""
    Write-Host "Content creation reported errors:" -ForegroundColor Red
    $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host "Full log: $LogFile"
    exit 1
}

if ($editorExit -ne 0) {
    Write-Host "Editor exited with code $editorExit. Full log: $LogFile" -ForegroundColor Red
    exit $editorExit
}

Write-Host ""
Write-Host "Content creation finished clean. Full log: $LogFile" -ForegroundColor Green
exit 0
