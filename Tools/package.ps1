# Packages the game for Windows: build, cook, stage, pak (IoStore), archive, then zip.
#
# Output: Saved\Packaged\Windows\Hawkeye.exe (runs without the engine installed) and
# Saved\Packaged\Hawkeye-win64-<date>.zip. Full UAT log in Saved\Logs\Package.log.
# The maps and the always-cook folders are in DefaultGame.ini (ProjectPackagingSettings).
# The Game Animation Sample content is gitignored but must be on disk (Tools\create-content.ps1);
# the cook reads it from Content\ like any other asset.
#
#   .\Tools\package.ps1              # Development: logs, console, automation tests available
#   .\Tools\package.ps1 -Shipping    # Shipping: no console, no logs by default
#   .\Tools\package.ps1 -NoZip
#
# Takes 15 to 40 minutes the first time (shader compiles), less once the DDC is warm.
# Refuses to run while an editor instance is open: 16 GB of RAM will not hold both.

[CmdletBinding()]
param(
    [switch]$Shipping,
    [switch]$NoZip,
    [switch]$NoIoStore,
    [string]$Engine = ""
)

# Engine root: -Engine, HAWKEYE_UE_ROOT, Tools\local.json, registry, launcher, default (hawkeye.env.ps1).
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Engine = Resolve-HawkeyeEngine -Explicit $Engine

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Proj = Join-Path $Root "Hawkeye.uproject"
$Uat = Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat"
$Archive = Join-Path $Root "Saved\Packaged"
$Log = Join-Path $Root "Saved\Logs\Package.log"
if (-not (Test-Path $Uat)) { throw "RunUAT.bat not found at $Uat" }
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    Write-Error "An editor instance is running. Close it before packaging."
    exit 1
}
New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
New-Item -ItemType Directory -Force $Archive | Out-Null

# Archiving copies over what is there, so a Shipping run after a Development one would leave both
# executables (and both .pdb files) in the folder and in the zip.
$Staged = Join-Path $Archive "Windows"
if (Test-Path $Staged) { Remove-Item $Staged -Recurse -Force }

$Config = if ($Shipping) { "Shipping" } else { "Development" }
$UatArgs = @(
    "BuildCookRun", "-project=$Proj", "-platform=Win64", "-clientconfig=$Config",
    "-build", "-cook", "-stage", "-pak", "-archive", "-archivedirectory=$Archive",
    "-prereqs", "-nop4", "-utf8output", "-unattended"
)
if (-not $NoIoStore) { $UatArgs += "-iostore" }

Write-Host "Packaging Hawkeye ($Config) into $Archive; log: $Log"
$start = Get-Date
# Windows PowerShell turns native stderr into errors; Stop would abort on the first one.
$ErrorActionPreference = "Continue"
& $Uat @UatArgs 2>&1 | ForEach-Object { "$_" } | Out-File -FilePath $Log -Encoding utf8
$ErrorActionPreference = "Stop"
$code = $LASTEXITCODE
$minutes = [math]::Round(((Get-Date) - $start).TotalMinutes, 1)
if ($code -ne 0) {
    Write-Host "BuildCookRun failed (exit $code) after $minutes min. Last errors:"
    Select-String -Path $Log -Pattern "Error:|error C|LogCook: Error|ExitCode=" | Select-Object -Last 20 | ForEach-Object { $_.Line }
    exit $code
}

$Exe = Join-Path $Archive "Windows\Hawkeye.exe"
$sizeGb = (Get-ChildItem (Join-Path $Archive "Windows") -Recurse -File | Measure-Object Length -Sum).Sum / 1GB
Write-Host ("Packaged in {0} min: {1} ({2:N2} GB)" -f $minutes, $Exe, $sizeGb)

if (-not $NoZip) {
    $Zip = Join-Path $Archive ("Hawkeye-win64-{0}{1}.zip" -f (Get-Date -Format "yyyy-MM-dd"), $(if ($Shipping) { "-shipping" } else { "" }))
    if (Test-Path $Zip) { Remove-Item $Zip -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $Archive "Windows"), $Zip,
        [System.IO.Compression.CompressionLevel]::Optimal, $true)
    Write-Host ("Zip: {0} ({1:N2} GB)" -f $Zip, ((Get-Item $Zip).Length / 1GB))
}
