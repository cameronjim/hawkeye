# Turns the downloaded combat clips into game animations: runs Tools\Editor\import_combat_anims.py
# in a headless editor (FBX import or package copy, IK rigs, IK retargeters, batch retarget,
# AM_ montages, the bow's aim offset blend spaces), then Tools\Editor\create_combat_anims.py to fill
# the DA_AnimSet_ data assets, Tools\Editor\create_bow_ik.py to wire the aim offsets into the bow
# hands post-process graphs and Tools\Editor\create_kate_look.py to draw Kate as Sparrow (once the copy is in).
# What to download and where: Tools\Data\Anims\README.md.
#
# Idempotent: existing assets are kept unless -Force. Missing clips are reported, not fatal.
# Do not run this while another editor instance is open (16 GB of RAM; they also collide on the DLL).
#
#   .\Tools\import-anims.ps1                  # import what is there, fill the anim sets
#   .\Tools\import-anims.ps1 -Force           # redo everything
#   .\Tools\import-anims.ps1 -Only Mixamo     # one source
#   .\Tools\import-anims.ps1 -SelfTest        # prove the pipeline on GASP's UE5 clips, the engine's FBX and Sparrow

[CmdletBinding()]
param(
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8",
    [string]$Only = "",
    [switch]$Force,
    [switch]$SelfTest,
    [switch]$Keep
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$Project = Join-Path $ProjectRoot "Hawkeye.uproject"
$EditorCmd = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$LogDir = Join-Path $ProjectRoot "Saved\Logs"

if (-not (Test-Path $EditorCmd)) { throw "Headless editor not found: $EditorCmd" }
if (-not (Test-Path $LogDir))    { New-Item -ItemType Directory -Path $LogDir | Out-Null }
if (Get-Process UnrealEditor* -ErrorAction SilentlyContinue) {
    Write-Warning "An editor instance is already running. Close it first; this machine has 16 GB RAM."
    exit 1
}

function Invoke-EditorPython([string]$ScriptName, [string]$LogName) {
    $script = Join-Path $ScriptDir "Editor\$ScriptName"
    $log = Join-Path $LogDir $LogName
    if (Test-Path $log) { Remove-Item $log -Force }
    Write-Host "Running $ScriptName $env:HAWKEYE_ANIM_ARGS ..."
    & $EditorCmd $Project -run=pythonscript "-script=$script" -unattended -nullrhi -nosplash -nop4 `
        -stdout -FullStdOutLogOutput -NoLogTimes "-abslog=$log" | Out-Null
    $exit = $LASTEXITCODE
    if (-not (Test-Path $log)) { throw "No log at $log" }
    $lines = Select-String -Path $log -Pattern "LogPython" | ForEach-Object { $_.Line }
    $lines | Where-Object { $_ -match "\[Hawkeye\]" } | ForEach-Object { Write-Host $_ }
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

$argsList = @()
if ($Force)    { $argsList += "--force" }
if ($SelfTest) { $argsList += "--selftest" }
if ($Keep)     { $argsList += "--keep" }
if ($Only)     { $argsList += @("--only", $Only) }
$env:HAWKEYE_ANIM_ARGS = ($argsList -join " ")
try {
    $result = Invoke-EditorPython "import_combat_anims.py" "HawkeyeAnims.log"
    if ($result -ne 0) { exit $result }
    if (-not $SelfTest) {
        $result = Invoke-EditorPython "create_combat_anims.py" "HawkeyeAnimSets.log"
        if ($result -ne 0) { exit $result }
        $result = Invoke-EditorPython "create_bow_ik.py" "HawkeyeBowIK.log"
        if ($result -ne 0) { exit $result }
        $result = Invoke-EditorPython "create_kate_look.py" "HawkeyeKateLook.log"
        if ($result -ne 0) { exit $result }
    }
} finally {
    Remove-Item Env:\HAWKEYE_ANIM_ARGS -ErrorAction SilentlyContinue
}

Write-Host "Combat animation import finished clean." -ForegroundColor Green
exit 0
