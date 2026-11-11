# Shared path resolver for every script in Tools\. Dot-source it, do not run it:
#
#   . (Join-Path $PSScriptRoot "hawkeye.env.ps1")
#   $Engine = Resolve-HawkeyeEngine -Explicit $Engine
#
# Engine root (UE 5.8), first hit wins; a candidate counts only if it holds Build.bat and
# UnrealEditor-Cmd.exe:
#   1. -Explicit (a script's -Engine argument)
#   2. env var HAWKEYE_UE_ROOT
#   3. Tools\local.json  {"engineRoot": "..."}   (git-ignored; copy Tools\local.example.json)
#   4. registry HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8  InstalledDirectory
#   5. the Epic launcher's LauncherInstalled.dat (AppName UE_5.8)
#   6. C:\Program Files\Epic Games\UE_5.8
#
# Game Animation Sample (GASP) root, first existing hit wins:
#   env var HAWKEYE_GASP_ROOT, Tools\local.json {"gaspRoot": "..."}, C:\Users\camer\code\GASP
#
# Asset packs root (the Quaternius CC0 packs Tools\Editor\import_bodies.py reads), first existing hit wins:
#   env var HAWKEYE_ASSETS_ROOT, Tools\local.json {"assetsRoot": "..."}, C:\Users\camer\code\hawkeye-assets
#
# Written for Windows PowerShell 5.1 as well as PowerShell 7.

$script:HawkeyeEngineVersion = "5.8"
$script:HawkeyeDefaultEngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$script:HawkeyeDefaultGaspRoot = "C:\Users\camer\code\GASP"
$script:HawkeyeDefaultAssetsRoot = "C:\Users\camer\code\hawkeye-assets"
$script:HawkeyeToolsDir = $PSScriptRoot

function Get-HawkeyeRepoRoot {
    [CmdletBinding()]
    param([string]$ToolsDir = $script:HawkeyeToolsDir)
    return [System.IO.Path]::GetFullPath((Join-Path $ToolsDir ".."))
}

function Read-HawkeyeLocalConfig {
    [CmdletBinding()]
    param([string]$Path = (Join-Path $script:HawkeyeToolsDir "local.json"))
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    } catch {
        throw "Could not parse $Path as JSON: $($_.Exception.Message)"
    }
}

function Test-HawkeyeEngineRoot {
    param([string]$Root)
    if (-not $Root) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Root "Engine\Build\BatchFiles\Build.bat")) -and
           (Test-Path -LiteralPath (Join-Path $Root "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"))
}

function Resolve-HawkeyeEngine {
    [CmdletBinding()]
    param(
        [string]$Explicit = "",
        [string]$LocalJsonPath = (Join-Path $script:HawkeyeToolsDir "local.json"),
        [string]$RegistryKey = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$($script:HawkeyeEngineVersion)",
        [string]$LauncherInstalledPath = (Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"),
        [string]$DefaultRoot = $script:HawkeyeDefaultEngineRoot
    )

    # An explicit argument is the caller's word; the script's own existence check reports a bad one.
    if ($Explicit) { return $Explicit }

    $tried = New-Object System.Collections.Generic.List[string]
    $candidates = @()

    $envRoot = $env:HAWKEYE_UE_ROOT
    # A path the user set on purpose (env var, local.json) must be right: it fails loudly instead of
    # quietly falling through to some other install.
    if ($envRoot) {
        if (-not (Test-HawkeyeEngineRoot -Root $envRoot)) {
            throw ("HAWKEYE_UE_ROOT is set to '$envRoot', but there is no Unreal Engine $($script:HawkeyeEngineVersion) there" +
                   " (needs Engine\Build\BatchFiles\Build.bat and Engine\Binaries\Win64\UnrealEditor-Cmd.exe)." +
                   [Environment]::NewLine + "Fix: correct HAWKEYE_UE_ROOT, or clear it to use Tools\local.json, the registry or the launcher.")
        }
        return $envRoot
    }
    else { $tried.Add("env HAWKEYE_UE_ROOT: not set") }

    $cfg = Read-HawkeyeLocalConfig -Path $LocalJsonPath
    if ($cfg -and $cfg.engineRoot) {
        if (-not (Test-HawkeyeEngineRoot -Root ([string]$cfg.engineRoot))) {
            throw ("engineRoot in $LocalJsonPath is '$($cfg.engineRoot)', but there is no Unreal Engine $($script:HawkeyeEngineVersion) there" +
                   " (needs Engine\Build\BatchFiles\Build.bat and Engine\Binaries\Win64\UnrealEditor-Cmd.exe)." +
                   [Environment]::NewLine + "Fix: correct engineRoot in that file, or delete the key to use the registry or the launcher.")
        }
        return [string]$cfg.engineRoot
    }
    elseif ($cfg) { $tried.Add("local.json ${LocalJsonPath}: no engineRoot key") }
    else { $tried.Add("local.json ${LocalJsonPath}: file not found") }

    $reg = $null
    try { $reg = (Get-ItemProperty -LiteralPath $RegistryKey -Name InstalledDirectory -ErrorAction Stop).InstalledDirectory } catch { $reg = $null }
    if ($reg) { $candidates += , @("registry $RegistryKey", [string]$reg) }
    else { $tried.Add("registry ${RegistryKey}: InstalledDirectory not found") }

    $launcher = $null
    if ($LauncherInstalledPath -and (Test-Path -LiteralPath $LauncherInstalledPath)) {
        try {
            $dat = Get-Content -LiteralPath $LauncherInstalledPath -Raw | ConvertFrom-Json
            $hit = @($dat.InstallationList | Where-Object { $_.AppName -eq "UE_$($script:HawkeyeEngineVersion)" }) | Select-Object -First 1
            if ($hit) { $launcher = [string]$hit.InstallLocation }
        } catch { $launcher = $null }
    }
    if ($launcher) { $candidates += , @("launcher $LauncherInstalledPath", $launcher) }
    else { $tried.Add("launcher ${LauncherInstalledPath}: no UE_$($script:HawkeyeEngineVersion) entry") }

    $candidates += , @("default path", $DefaultRoot)

    foreach ($c in $candidates) {
        if (Test-HawkeyeEngineRoot -Root $c[1]) { return $c[1] }
        $tried.Add("$($c[0]) = $($c[1]): no Engine\Build\BatchFiles\Build.bat and Engine\Binaries\Win64\UnrealEditor-Cmd.exe there")
    }

    $list = ($tried | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    throw ("Unreal Engine $($script:HawkeyeEngineVersion) not found. Tried, in order:" + [Environment]::NewLine + $list +
           [Environment]::NewLine + "Fix: set HAWKEYE_UE_ROOT, or copy Tools\local.example.json to Tools\local.json and set engineRoot, or pass -Engine.")
}

function Resolve-HawkeyeGaspRoot {
    [CmdletBinding()]
    param(
        [string]$LocalJsonPath = (Join-Path $script:HawkeyeToolsDir "local.json"),
        [string]$DefaultRoot = $script:HawkeyeDefaultGaspRoot,
        [switch]$Required
    )
    $candidates = @()
    if ($env:HAWKEYE_GASP_ROOT) { $candidates += , @("env HAWKEYE_GASP_ROOT", $env:HAWKEYE_GASP_ROOT) }
    $cfg = Read-HawkeyeLocalConfig -Path $LocalJsonPath
    if ($cfg -and $cfg.gaspRoot) { $candidates += , @("gaspRoot in $LocalJsonPath", [string]$cfg.gaspRoot) }
    $candidates += , @("default path", $DefaultRoot)

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $c[1] "Content")) { return $c[1] }
    }
    if ($Required) {
        $list = ($candidates | ForEach-Object { "  - $($_[0]) = $($_[1]): no Content folder" }) -join [Environment]::NewLine
        throw ("Game Animation Sample (GASP) not found. Tried, in order:" + [Environment]::NewLine + $list +
               [Environment]::NewLine + "Fix: create it from the Epic Launcher's Samples tab, then set HAWKEYE_GASP_ROOT or gaspRoot in Tools\local.json.")
    }
    # Not fatal for most scripts: hand back the highest-priority configured path; the importer reports it missing.
    return $candidates[0][1]
}

function Resolve-HawkeyeAssetsRoot {
    [CmdletBinding()]
    param(
        [string]$LocalJsonPath = (Join-Path $script:HawkeyeToolsDir "local.json"),
        [string]$DefaultRoot = $script:HawkeyeDefaultAssetsRoot
    )
    $candidates = @()
    if ($env:HAWKEYE_ASSETS_ROOT) { $candidates += , @("env HAWKEYE_ASSETS_ROOT", $env:HAWKEYE_ASSETS_ROOT) }
    $cfg = Read-HawkeyeLocalConfig -Path $LocalJsonPath
    if ($cfg -and $cfg.assetsRoot) { $candidates += , @("assetsRoot in $LocalJsonPath", [string]$cfg.assetsRoot) }
    $candidates += , @("default path", $DefaultRoot)

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c[1] -PathType Container) { return $c[1] }
    }
    # Never fatal: without the packs import_bodies.py logs one line and the mannequins stay.
    return $candidates[0][1]
}

function Get-HawkeyeBuildBat {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$EngineRoot)
    return (Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat")
}

function Get-HawkeyeEditorCmd {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$EngineRoot)
    return (Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
}
