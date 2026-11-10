# Runs Hawkeye automation tests headless and exits non-zero on any failure.
# Usage: .\Tools\run-tests.ps1 [-Filter Hawkeye.Health] [-Engine "<ue root>"]
#        [-LogFile <path>] [-Report <dir>]   (so two runs on this machine don't write over each other's)
param(
    [string]$Filter = "Hawkeye",
    [string]$Engine = "",
    [string]$LogFile = "",
    [string]$Report = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Proj = Join-Path $Root "Hawkeye.uproject"
if (-not $Report) { $Report = Join-Path $Root "Saved\Automation" }
$Cmd = Get-HawkeyeEditorCmd -EngineRoot $Engine

if (-not (Test-Path $Cmd)) { Write-Error "UnrealEditor-Cmd.exe not found at $Cmd"; exit 2 }
if (Test-Path $Report) { Remove-Item -Recurse -Force $Report }

if (-not $LogFile) { $LogFile = Join-Path $Root "Saved\Logs\HawkeyeTests.log" }
& $Cmd $Proj -ExecCmds="Automation RunTests $Filter; Quit" -unattended -nullrhi -nosplash -nop4 -stdout -FullStdOutLogOutput -NoLogTimes -ReportExportPath="$Report" -abslog="$LogFile" | Out-Null

if (-not (Test-Path $LogFile)) { Write-Error "No log produced at $LogFile"; exit 2 }

$Lines = Get-Content $LogFile
# UE 5.8 writes Result={Success} / Result={Fail} / Result={Skipped}, not Passed/Failed.
$Passed = @($Lines | Select-String -Pattern "Test Completed\. Result=\{(Success|Passed)\}").Count
$Failed = @($Lines | Select-String -Pattern "Test Completed\. Result=\{(Fail|Failed)\}")
$Skipped = @($Lines | Select-String -Pattern "Test Completed\. Result=\{(Skipped|NotRun)\}").Count

Write-Host ""
Write-Host "Hawkeye tests ($Filter): $Passed passed, $($Failed.Count) failed, $Skipped skipped"
if ($Failed.Count -gt 0) {
    Write-Host ""
    Write-Host "Failures:"
    $Failed | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
    Write-Host ""
    Write-Host "Errors:"
    $Lines | Select-String -Pattern "LogAutomationController: Error|Error: .*Hawkeye" | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
    exit 1
}
if ($Passed -eq 0) {
    Write-Warning "No tests ran. Check the filter '$Filter' and that the Hawkeye module loaded. Log: $LogFile"
    exit 1
}
exit 0
