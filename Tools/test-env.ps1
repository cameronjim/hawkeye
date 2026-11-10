# Self-test for Tools\hawkeye.env.ps1. No engine, no editor: it builds fake engine and sample
# folders in a temp dir and checks the resolver's precedence. Exits 1 on any failure.
#
#   .\Tools\test-env.ps1

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")

$script:Failures = 0
$script:Checks = 0
function Assert-Equal($Name, $Expected, $Actual) {
    $script:Checks++
    if ($Expected -ne $Actual) {
        $script:Failures++
        Write-Host "FAIL $Name`n   expected: $Expected`n   actual:   $Actual" -ForegroundColor Red
    } else {
        Write-Host "ok   $Name"
    }
}
function Assert-Throws($Name, $Needles, [scriptblock]$Body) {
    $script:Checks++
    $msg = $null
    try { & $Body | Out-Null } catch { $msg = $_.Exception.Message }
    if (-not $msg) { $script:Failures++; Write-Host "FAIL $Name (did not throw)" -ForegroundColor Red; return }
    foreach ($n in $Needles) {
        if ($msg -notlike "*$n*") { $script:Failures++; Write-Host "FAIL $Name (message lacks '$n'):`n$msg" -ForegroundColor Red; return }
    }
    Write-Host "ok   $Name"
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("hawkeye-env-test-" + [guid]::NewGuid().ToString("N"))
$regKey = "HKCU:\Software\HawkeyeEnvTest-" + [guid]::NewGuid().ToString("N")
$savedUe = $env:HAWKEYE_UE_ROOT
$savedGasp = $env:HAWKEYE_GASP_ROOT

function New-FakeEngine($Name) {
    $root = Join-Path $tmp $Name
    New-Item -ItemType Directory -Force -Path (Join-Path $root "Engine\Build\BatchFiles") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $root "Engine\Binaries\Win64") | Out-Null
    Set-Content -LiteralPath (Join-Path $root "Engine\Build\BatchFiles\Build.bat") -Value "rem fake"
    Set-Content -LiteralPath (Join-Path $root "Engine\Binaries\Win64\UnrealEditor-Cmd.exe") -Value "fake"
    return $root
}

try {
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $eEnv = New-FakeEngine "e-env"
    $eLocal = New-FakeEngine "e-local"
    $eReg = New-FakeEngine "e-reg"
    $eLauncher = New-FakeEngine "e-launcher"
    $eDefault = New-FakeEngine "e-default"
    $eBad = Join-Path $tmp "e-missing"

    $localJson = Join-Path $tmp "local.json"
    $noLocal = Join-Path $tmp "absent-local.json"
    $dat = Join-Path $tmp "LauncherInstalled.dat"
    $noDat = Join-Path $tmp "absent.dat"
    $noReg = "HKCU:\Software\HawkeyeEnvTest-does-not-exist"

    ConvertTo-Json @{ engineRoot = $eLocal } | Set-Content -LiteralPath $localJson
    New-Item -Path $regKey -Force | Out-Null
    New-ItemProperty -Path $regKey -Name InstalledDirectory -Value $eReg | Out-Null
    ConvertTo-Json @{ InstallationList = @(
        @{ AppName = "UE_5.7"; InstallLocation = (Join-Path $tmp "wrong-version") },
        @{ AppName = "UE_5.8"; InstallLocation = $eLauncher }) } | Set-Content -LiteralPath $dat

    function Resolve-Fake($Explicit = "", $Local = $noLocal, $Reg = $noReg, $Launcher = $noDat) {
        Resolve-HawkeyeEngine -Explicit $Explicit -LocalJsonPath $Local -RegistryKey $Reg -LauncherInstalledPath $Launcher -DefaultRoot $eDefault
    }

    $env:HAWKEYE_UE_ROOT = $eEnv
    Assert-Equal "explicit beats everything" "C:\explicit" (Resolve-Fake -Explicit "C:\explicit" -Local $localJson -Reg $regKey -Launcher $dat)
    Assert-Equal "env beats local, registry, launcher, default" $eEnv (Resolve-Fake -Local $localJson -Reg $regKey -Launcher $dat)

    $env:HAWKEYE_UE_ROOT = $eBad
    Assert-Equal "invalid env falls through to local.json" $eLocal (Resolve-Fake -Local $localJson -Reg $regKey -Launcher $dat)

    Remove-Item Env:\HAWKEYE_UE_ROOT
    Assert-Equal "local.json beats registry" $eLocal (Resolve-Fake -Local $localJson -Reg $regKey -Launcher $dat)
    Assert-Equal "registry beats launcher" $eReg (Resolve-Fake -Reg $regKey -Launcher $dat)
    Assert-Equal "launcher beats default and skips other versions" $eLauncher (Resolve-Fake -Launcher $dat)
    Assert-Equal "default last" $eDefault (Resolve-Fake)

    ConvertTo-Json @{ engineRoot = $eBad } | Set-Content -LiteralPath $localJson
    Assert-Equal "invalid local.json engineRoot falls through" $eReg (Resolve-Fake -Local $localJson -Reg $regKey -Launcher $dat)

    Set-Content -LiteralPath $localJson -Value "{ not json"
    Assert-Throws "malformed local.json names the file" @("local.json", "parse") { Resolve-Fake -Local $localJson }

    $env:HAWKEYE_UE_ROOT = $eBad
    Assert-Throws "nothing valid lists every source tried" @("HAWKEYE_UE_ROOT", "local.json", "registry", "launcher", "default path", "e-missing") {
        Resolve-HawkeyeEngine -LocalJsonPath $noLocal -RegistryKey $noReg -LauncherInstalledPath $noDat -DefaultRoot $eBad
    }
    Remove-Item Env:\HAWKEYE_UE_ROOT

    Assert-Equal "Build.bat path" (Join-Path $eEnv "Engine\Build\BatchFiles\Build.bat") (Get-HawkeyeBuildBat -EngineRoot $eEnv)
    Assert-Equal "UnrealEditor-Cmd.exe path" (Join-Path $eEnv "Engine\Binaries\Win64\UnrealEditor-Cmd.exe") (Get-HawkeyeEditorCmd -EngineRoot $eEnv)
    Assert-Equal "repo root from Tools dir" (Split-Path -Parent $PSScriptRoot) (Get-HawkeyeRepoRoot)

    # GASP
    $gEnv = Join-Path $tmp "g-env"; $gLocal = Join-Path $tmp "g-local"; $gDefault = Join-Path $tmp "g-default"
    foreach ($g in $gEnv, $gLocal, $gDefault) { New-Item -ItemType Directory -Force -Path (Join-Path $g "Content") | Out-Null }
    ConvertTo-Json @{ gaspRoot = $gLocal } | Set-Content -LiteralPath $localJson
    $env:HAWKEYE_GASP_ROOT = $gEnv
    Assert-Equal "gasp env first" $gEnv (Resolve-HawkeyeGaspRoot -LocalJsonPath $localJson -DefaultRoot $gDefault)
    Remove-Item Env:\HAWKEYE_GASP_ROOT
    Assert-Equal "gasp local.json second" $gLocal (Resolve-HawkeyeGaspRoot -LocalJsonPath $localJson -DefaultRoot $gDefault)
    Assert-Equal "gasp default third" $gDefault (Resolve-HawkeyeGaspRoot -LocalJsonPath $noLocal -DefaultRoot $gDefault)
    $env:HAWKEYE_GASP_ROOT = (Join-Path $tmp "g-missing")
    Assert-Equal "gasp missing env falls through to local.json" $gLocal (Resolve-HawkeyeGaspRoot -LocalJsonPath $localJson -DefaultRoot $gDefault)
    Assert-Throws "gasp -Required names what was tried" @("HAWKEYE_GASP_ROOT", "g-missing", "default path") {
        Resolve-HawkeyeGaspRoot -LocalJsonPath $noLocal -DefaultRoot (Join-Path $tmp "g-none") -Required
    }
    Assert-Equal "gasp not required returns first configured" (Join-Path $tmp "g-missing") (Resolve-HawkeyeGaspRoot -LocalJsonPath $noLocal -DefaultRoot (Join-Path $tmp "g-none"))
}
finally {
    if ($null -eq $savedUe) { Remove-Item Env:\HAWKEYE_UE_ROOT -ErrorAction SilentlyContinue } else { $env:HAWKEYE_UE_ROOT = $savedUe }
    if ($null -eq $savedGasp) { Remove-Item Env:\HAWKEYE_GASP_ROOT -ErrorAction SilentlyContinue } else { $env:HAWKEYE_GASP_ROOT = $savedGasp }
    if (Test-Path $regKey) { Remove-Item -Path $regKey -Recurse -Force }
    if (Test-Path $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
}

Write-Host ""
if ($script:Failures -gt 0) {
    Write-Host "$script:Failures of $script:Checks checks failed." -ForegroundColor Red
    exit 1
}
Write-Host "All $script:Checks checks passed." -ForegroundColor Green
exit 0
