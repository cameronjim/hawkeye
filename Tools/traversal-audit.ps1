# Runs the grapple audit headless (UHawkeyeTraversalAuditCommandlet) on a map and exits with its gate:
# 0 passed, 1 a street-facing roof nothing reaches from the street (or the objective chain broke), 2 could not run.
# Usage: .\Tools\traversal-audit.ps1 [-Map /Game/Maps/L_District_EastVillage] [-Out <json>] [-Chain a,b,c]
#        [-StreetLinks c] [-AllowFile <txt>] [-NoSpawn] [-Engine "<ue root>"] [-LogFile <path>]
# The JSON has grapple_audit.json's keys (claude-docs/tools-traversal-debugger.md). Allow file: one osm id per line,
# '#' starts a comment; Tools\traversal_audit_allow.txt is used when it exists and -AllowFile is not given.
param(
    [string]$Map = "/Game/Maps/L_District_EastVillage",
    [string]$Out = "",
    [string]$Chain = "reach_roof,cross_block,find_arrow",
    [string]$StreetLinks = "find_arrow",
    [string]$AllowFile = "",
    [switch]$NoSpawn,
    [string]$Engine = "",
    [string]$LogFile = "",
    # Pair probe: "x y anchor" lines (street point, anchor index) evaluated with the game's traces into a TSV.
    [string]$Probe = "",
    [string]$ProbeOut = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$Root = Get-HawkeyeRepoRoot
$Proj = Join-Path $Root "Hawkeye.uproject"
$Cmd = Get-HawkeyeEditorCmd -EngineRoot $Engine
if (-not (Test-Path $Cmd)) { Write-Error "UnrealEditor-Cmd.exe not found at $Cmd"; exit 2 }

if (-not $Out) { $Out = Join-Path $Root "Saved\Automation\traversal_audit.json" }
if (-not $LogFile) { $LogFile = Join-Path $Root "Saved\Logs\HawkeyeTraversalAudit.log" }
if (-not $AllowFile) {
    $Default = Join-Path $PSScriptRoot "traversal_audit_allow.txt"
    if (Test-Path $Default) { $AllowFile = $Default }
}

$CmdArgs = @($Proj, "-run=HawkeyeTraversalAudit", "-map=$Map", "-out=$Out", "-unattended", "-nullrhi", "-nosplash", "-nop4",
    "-stdout", "-FullStdOutLogOutput", "-NoLogTimes", "-abslog=$LogFile")
if ($Chain) { $CmdArgs += "-chain=$Chain" }
if ($StreetLinks) { $CmdArgs += "-streetlinks=$StreetLinks" }
if ($AllowFile) { $CmdArgs += "-allowfile=$AllowFile" }
if ($NoSpawn) { $CmdArgs += "-nospawn" }
if ($Probe) { $CmdArgs += "-probe=$Probe" }
if ($ProbeOut) { $CmdArgs += "-probeout=$ProbeOut" }

$Began = Get-Date
& $Cmd @CmdArgs | Out-Null
$Code = $LASTEXITCODE
$Seconds = [int]((Get-Date) - $Began).TotalSeconds

if (-not (Test-Path $LogFile)) { Write-Error "No log produced at $LogFile"; exit 2 }
$Lines = Get-Content $LogFile | Where-Object { $_ -match "LogHawkeyeEditor: " -and $_ -notmatch "^LogInit" }
Write-Host ""
$Lines | Where-Object { $_ -match "Traversal audit: (\d+ anchors|PASSED|FAILED|wrote|reach_|cross_|find_)" } |
    ForEach-Object { Write-Host ("  " + ($_ -replace "^.*Traversal audit: ", "")) }
$Errors = @($Lines | Where-Object { $_ -match "Error: " })
if ($Errors.Count -gt 0) {
    Write-Host ""
    Write-Host "Errors:"
    $Errors | ForEach-Object { Write-Host ("  " + ($_ -replace "^.*Traversal audit: ", "")) }
}
Write-Host ""
Write-Host "Traversal audit ($Map): exit $Code in $Seconds s. JSON: $Out  Log: $LogFile"
exit $Code
