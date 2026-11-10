# Runs the pure-Python unit tests for Tools\Editor\_geo.py with the engine's bundled Python.
# No editor, no unreal module: these are the projection and polygon helpers the city
# generator is built on.
#
#   .\Tools\test-geo.ps1

[CmdletBinding()]
param(
    [string]$Engine = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Python = Join-Path $Engine "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
$Tests = Join-Path $ScriptDir "Editor\test_geo.py"

if (-not (Test-Path $Python)) { Write-Error "Engine Python not found: $Python"; exit 2 }

& $Python $Tests
exit $LASTEXITCODE
