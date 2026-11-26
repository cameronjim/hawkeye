# The per-change check. Reads what changed, picks the steps that change needs (Tools\Gate\select.py from
# Tools\Gate\rules.json) and runs only those. See claude-docs/testing.md, "The gate".
#
#   .\Tools\gate.ps1                       # quick tier: the plan for what changed since the last pass (the whole per-change rule)
#   .\Tools\gate.ps1 -Plan                 # print the plan and its estimated time, run nothing
#   .\Tools\gate.ps1 -Status               # last pass and last full, commits since the full, risky changes since it
#   .\Tools\gate.ps1 -Base 905cb20         # changes since that commit instead of since the last pass
#   .\Tools\gate.ps1 -Tier full            # everything: both builds, the suite, content twice, city, audit, 13 laps (rarely)
#
# Changed files: git diff --name-only <Base>..HEAD plus uncommitted tracked changes. Base defaults to the commit in
# Saved\Gate\last-pass.json when it is an ancestor of HEAD, else origin/main.
#
# One heavy process at a time. Before every engine launch the script runs `tasklist`; an Unreal (or Blender) it did not
# start means the owner is playing or working, so it waits and never kills it (-NoWait fails instead). Each step prints
# one PASS or FAIL line with its seconds; the exit code is non-zero on any failure or any "Fatal error!" in a step's log.
# Nothing here touches Saved\SaveGames: the engine's automation runs use the *Automation save slots.

[CmdletBinding()]
param(
    [ValidateSet("quick", "full")]
    [string]$Tier = "quick",
    [string]$Base = "",
    [switch]$Plan,
    [switch]$Status,
    # Stop at the first failed step instead of running the rest.
    [switch]$FailFast,
    # Fail at once when another Unreal or Blender process is running, instead of waiting for it to close.
    [switch]$NoWait,
    [string]$Engine = ""
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "hawkeye.env.ps1")
$Root = Get-HawkeyeRepoRoot
$Proj = Join-Path $Root "Hawkeye.uproject"
$GateDir = Join-Path $Root "Saved\Gate"
$LogDir = Join-Path $GateDir "logs"
$Rules = Join-Path $PSScriptRoot "Gate\rules.json"
$SelectPy = Join-Path $PSScriptRoot "Gate\select.py"
$LastPass = Join-Path $GateDir "last-pass.json"
$LastFull = Join-Path $GateDir "last-full.json"
$TimingsFile = Join-Path $GateDir "timings.json"
New-Item -ItemType Directory -Force $LogDir | Out-Null
Set-Location $Root

$RulesData = Get-Content $Rules -Raw -Encoding UTF8 | ConvertFrom-Json

# ---------------------------------------------------------------------------------------------------- helpers

function Get-PythonExe {
    $c = Get-Command python, py -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch "WindowsApps" } | Select-Object -First 1
    if (-not $c) { throw "No python on PATH (the Microsoft Store alias does not count)." }
    return $c.Source
}

function Format-Seconds([double]$s) {
    $s = [int]$s
    if ($s -ge 60) { return ("{0}m{1:00}s" -f [int][math]::Floor($s / 60), ($s % 60)) }
    return "${s}s"
}

function Get-GitLines([string[]]$GitArgs) {
    $out = & git -c core.quotepath=off @GitArgs 2>$null
    return @($out | Where-Object { $_ })
}

function Get-ShortCommit([string]$rev) {
    $h = (& git rev-parse --short=7 $rev 2>$null)
    return "$h".Trim()
}

function Read-JsonFile([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    try { return (Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json) } catch { return $null }
}

function Get-ChangedFiles([string]$From) {
    $a = Get-GitLines @("diff", "--name-only", "--no-renames", "$From..HEAD")
    $b = Get-GitLines @("diff", "--name-only", "--no-renames", "HEAD")
    return @(($a + $b) | Select-Object -Unique)
}

function Resolve-Base {
    if ($Base) { return $Base }
    $last = Read-JsonFile $LastPass
    if ($last -and $last.commit) {
        & git merge-base --is-ancestor $last.commit HEAD 2>$null
        if ($LASTEXITCODE -eq 0) { return $last.commit }
        Write-Host "last-pass.json names $($last.commit), which is not an ancestor of HEAD; using origin/main."
    }
    & git rev-parse --verify --quiet origin/main 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "No origin/main to compare against; pass -Base <rev>." }
    return "origin/main"
}

function Get-Selection([string[]]$Changed) {
    $py = Get-PythonExe
    $text = ($Changed -join "`n")
    $json = $text | & $py -I -B $SelectPy --rules $Rules --timings $TimingsFile --repo $Root | Out-String
    if (-not $json.Trim()) { throw "select.py printed nothing." }
    return ($json | ConvertFrom-Json)
}

function Get-FullSteps {
    $est = $RulesData.estimates
    $steps = @()
    $steps += [pscustomobject]@{ id = "python"; label = "python tests (all suites) and ruff"; seconds = [int]$est.python + [int]$est.'pythonSuite:Tools/Editor' }
    $steps += [pscustomobject]@{ id = "build"; label = "build HawkeyeEditor"; seconds = [int]$est.build }
    $steps += [pscustomobject]@{ id = "buildGame"; label = "build Hawkeye (game target)"; seconds = [int]$est.buildGame }
    $steps += [pscustomobject]@{ id = "suite"; label = "automation suite (whole)"; seconds = [int]$est.suite }
    $steps += [pscustomobject]@{ id = "content1"; label = "create-content, first run"; seconds = [int]$est.content }
    $steps += [pscustomobject]@{ id = "content2"; label = "create-content, second run (clean)"; seconds = [int]$est.content }
    $steps += [pscustomobject]@{ id = "contentVerify"; label = "verify_content"; seconds = [int]$est.contentVerify }
    $steps += [pscustomobject]@{ id = "cityVerify"; label = "city generate and verify"; seconds = [int]$est.cityVerify }
    $steps += [pscustomobject]@{ id = "audit"; label = "traversal audit (expected anchors and pairs)"; seconds = [int]$est.audit }
    foreach ($lap in $RulesData.fullLaps) { $steps += [pscustomobject]@{ id = "lap:$lap"; label = "lap $lap"; seconds = [int]$est.lap } }
    return $steps
}

function Show-Plan($plan, $steps, [string]$From, [string]$Mode) {
    Write-Host ""
    Write-Host "Gate plan ($Mode), changes since $From`: $(@($plan.changed).Count) files"
    if ($Mode -eq "quick") {
        $byRules = @{}
        foreach ($m in $plan.matched) { foreach ($r in $m.rules) { if (-not $byRules.ContainsKey($r)) { $byRules[$r] = 0 }; $byRules[$r]++ } }
        foreach ($k in ($byRules.Keys | Sort-Object)) { Write-Host ("  {0,3} file(s)  {1}" -f $byRules[$k], $k) }
        if ($plan.broad) { Write-Host "  broad: a path no rule covers, or a rule that asks for the whole suite" }
        if ($plan.risky) { Write-Host "  risky: $(@($plan.riskyRules) -join '; ')" }
        if ($plan.tests -and @($plan.tests).Count -gt 0) { Write-Host "  filters: $(@($plan.tests) -join ' + ')" }
    }
    if (@($steps).Count -eq 0) { Write-Host "  nothing to run."; return }
    $total = 0
    foreach ($s in $steps) { Write-Host ("  {0,6}  {1}" -f (Format-Seconds $s.seconds), $s.label); $total += $s.seconds }
    Write-Host ("  ------`n  {0,6}  estimated total" -f (Format-Seconds $total))
}

# ---------------------------------------------------------------------------------------------------- -Status

if ($Status) {
    $pass = Read-JsonFile $LastPass
    $full = Read-JsonFile $LastFull
    Write-Host ""
    if ($pass) { Write-Host ("Last pass : {0} ({1}, {2}, {3})" -f $pass.commit, $pass.tier, $pass.time, (Format-Seconds $pass.seconds)) }
    else { Write-Host "Last pass : none recorded (Saved\Gate\last-pass.json)" }
    if ($full) { Write-Host ("Last full : {0} ({1}, {2})" -f $full.commit, $full.time, (Format-Seconds $full.seconds)) }
    else { Write-Host "Last full : none recorded (Saved\Gate\last-full.json)" }
    if ($pass) {
        $since = @(Get-GitLines @("rev-list", "--count", "$($pass.commit)..HEAD"))
        Write-Host "Commits since the last pass: $($since[0])"
    }
    if ($full) {
        & git merge-base --is-ancestor $full.commit HEAD 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "The last full's commit is not an ancestor of HEAD; run a full before a milestone."
        } else {
            $n = @(Get-GitLines @("rev-list", "--count", "$($full.commit)..HEAD"))
            Write-Host "Commits since the last full: $($n[0])"
            $plan = Get-Selection (Get-ChangedFiles $full.commit)
            if ($plan.risky) {
                Write-Host "RISKY changes since the last full: $(@($plan.riskyRules) -join '; ')  -> run .\Tools\gate.ps1 -Tier full"
            } else {
                Write-Host "No risky change since the last full."
            }
            if ($plan.broad) { Write-Host "Broad changes since the last full (config, project, build files or unknown paths): a full is due." }
        }
    } else {
        Write-Host "A full run is due before a milestone or the owner recording."
    }
    exit 0
}

# ---------------------------------------------------------------------------------------------------- -Plan

if ($Plan) {
    if ($Tier -eq "full") { Show-Plan $null (Get-FullSteps) "everything" "full"; exit 0 }
    $from = Resolve-Base
    $changed = Get-ChangedFiles $from
    $sel = Get-Selection $changed
    Show-Plan $sel $sel.steps (Get-ShortCommit $from) "quick"
    exit 0
}

# ---------------------------------------------------------------------------------------------------- running

$script:Results = @()
$script:Halted = $false
$script:Timings = @{}
$script:EngineRoot = ""
$script:Began = Get-Date

function Get-EngineRoot {
    if (-not $script:EngineRoot) { $script:EngineRoot = Resolve-HawkeyeEngine -Explicit $Engine }
    return $script:EngineRoot
}

function Get-HeavyProcesses {
    $out = (& tasklist /NH 2>$null) -join "`n"
    return @([regex]::Matches($out, "(?im)^(UnrealEditor(-Cmd)?|blender)\.exe\s") | ForEach-Object { $_.Value.Trim() })
}

# Before every engine launch. Anything running now is not ours (steps run one at a time and wait for their process).
function Wait-MachineFree {
    $waited = 0
    while ($true) {
        $heavy = Get-HeavyProcesses
        if ($heavy.Count -eq 0) { return }
        if ($NoWait) { throw "another heavy process is running ($($heavy -join ', ')); -NoWait" }
        if ($waited -eq 0) { Write-Host "  waiting: $($heavy -join ', ') is running and is not ours (the owner may be playing); never killed" }
        if ($waited -ge 7200) { throw "still waiting after 2 hours for $($heavy -join ', ')" }
        Start-Sleep -Seconds 30
        $waited += 30
    }
}

function Test-FatalLog([string[]]$Paths) {
    foreach ($p in $Paths) {
        if ($p -and (Test-Path $p)) {
            $hit = Select-String -Path $p -Pattern "Fatal error!" -SimpleMatch -List | Select-Object -First 1
            if ($hit) { return "Fatal error! in $p" }
        }
    }
    return ""
}

function New-Ok([string]$Detail = "") { return [pscustomobject]@{ Ok = $true; Detail = $Detail } }
function New-Bad([string]$Detail) { return [pscustomobject]@{ Ok = $false; Detail = $Detail } }

function Invoke-GateStep([string]$Id, [string]$Label, [scriptblock]$Body) {
    if ($script:Halted) {
        Write-Host ("SKIP  {0}" -f $Label)
        $script:Results += [pscustomobject]@{ id = $Id; label = $Label; ok = $false; skipped = $true; seconds = 0; detail = "skipped after a failure" }
        return
    }
    $t0 = Get-Date
    $result = $null
    try { $result = & $Body } catch { $result = New-Bad ("exception: " + $_.Exception.Message) }
    $sec = ((Get-Date) - $t0).TotalSeconds
    if ($result -is [array]) { $result = $result[-1] }
    if ($null -eq $result -or $null -eq $result.Ok) { $result = New-Bad "step returned nothing" }
    $word = if ($result.Ok) { "PASS" } else { "FAIL" }
    Write-Host ("{0}  {1}  {2}  {3}" -f $word, $Label, (Format-Seconds $sec), $result.Detail)
    $script:Results += [pscustomobject]@{ id = $Id; label = $Label; ok = [bool]$result.Ok; skipped = $false; seconds = [int]$sec; detail = $result.Detail }
    # An incremental build that had nothing to compile takes 2 seconds and would make every later estimate a lie: not fed back.
    if ($result.Ok -and $Id -notin @("build", "buildGame")) { $script:Timings[$Id] = [int]$sec }
    elseif ($FailFast) { $script:Halted = $true }
}

# Runs a native command through cmd so stderr cannot turn into a PowerShell error, output to a log. Returns the exit code.
function Invoke-ToLog([string]$CommandLine, [string]$Log) {
    if (Test-Path $Log) { Remove-Item $Log -Force }
    # A batch file, so Windows PowerShell 5.1's argument quoting never touches the command line.
    $bat = Join-Path $GateDir "step.cmd"
    Set-Content -Path $bat -Value ("@echo off`r`n" + $CommandLine + " > `"" + $Log + "`" 2>&1") -Encoding ASCII
    & cmd.exe /c $bat
    return $LASTEXITCODE
}

function Get-LogTail([string]$Log, [int]$Lines = 6) {
    if (-not (Test-Path $Log)) { return "(no log $Log)" }
    return ((Get-Content $Log -Tail $Lines) -join " | ")
}

function Get-FirstMatch([string]$Log, [string]$Pattern) {
    if (-not (Test-Path $Log)) { return "" }
    $m = Select-String -Path $Log -Pattern $Pattern | Select-Object -First 1
    if ($m) { return $m.Line.Trim() }
    return ""
}

# A tracked-file snapshot of the places the content scripts write, to see whether a run changed anything.
function Get-ContentStatus {
    return ((Get-GitLines @("status", "--porcelain", "--untracked-files=no", "--", "Content", "Config", "Plugins", "Tools/Data")) -join "`n")
}

function Compare-ContentStatus([string]$Before, [string]$After) {
    if ($Before -eq $After) { return "" }
    $b = @($Before -split "`n" | Where-Object { $_ })
    $a = @($After -split "`n" | Where-Object { $_ })
    $new = @($a | Where-Object { $b -notcontains $_ })
    $first = (@($new | Select-Object -First 3)) -join "; "
    return ("left " + $new.Count + " changed path(s) not there before, first: " + $first)
}

# ---- steps

function Step-Python([string[]]$Suites) {
    $py = Get-PythonExe
    $procs = @()
    # One process a test file, all at once: this is pure CPU (no engine), and the five slow Editor files take about two
    # minutes together instead of nine in a row.
    foreach ($dir in $Suites) {
        $abs = Join-Path $Root $dir
        foreach ($f in @(Get-ChildItem $abs -Filter "test_*.py")) {
            $mod = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
            $log = Join-Path $LogDir ("python_{0}_{1}.log" -f ($dir -replace "/", "_"), $mod)
            $out = $log + ".out"
            $p = Start-Process -FilePath $py -ArgumentList @("-B", "-m", "unittest", $mod) -WorkingDirectory $abs -PassThru `
                -WindowStyle Hidden -RedirectStandardError $log -RedirectStandardOutput $out
            $null = $p.Handle   # a process that ends fast reports no ExitCode unless its handle was taken while it ran
            $procs += [pscustomobject]@{ Proc = $p; Log = $log; Name = "$dir/$mod" }
        }
    }
    $total = 0
    $bad = @()
    foreach ($e in $procs) {
        $e.Proc.WaitForExit()
        $ran = Get-FirstMatch $e.Log "^Ran \d+ tests?"
        $okRun = ($e.Proc.ExitCode -eq 0) -or (($null -eq $e.Proc.ExitCode) -and (Get-FirstMatch $e.Log "^OK") -ne "")
        if (-not $okRun) { $bad += ($e.Name + ": " + (Get-LogTail $e.Log 6)) }
        elseif ($ran -match "Ran (\d+)") { $total += [int]$Matches[1] }
    }
    if ($bad.Count -gt 0) { return (New-Bad ("python tests failed in " + ($bad -join " || "))) }
    $ruffLog = Join-Path $LogDir "ruff.log"
    $ruff = Get-Command ruff -ErrorAction SilentlyContinue | Select-Object -First 1
    $cmd = if ($ruff) { "`"$($ruff.Source)`" check Tools" } else { "`"$py`" -m ruff check Tools" }
    $code = Invoke-ToLog $cmd $ruffLog
    if ($code -ne 0) { return (New-Bad ("ruff: " + (Get-LogTail $ruffLog 8))) }
    return (New-Ok "$total python tests, ruff clean")
}

function Step-Build([string]$Target) {
    Wait-MachineFree
    $bat = Get-HawkeyeBuildBat (Get-EngineRoot)
    $log = Join-Path $LogDir "build_$Target.log"
    $code = Invoke-ToLog "`"$bat`" $Target Win64 Development -project=`"$Proj`" -waitmutex" $log
    $fatal = Test-FatalLog @($log)
    if ($code -ne 0) {
        $err = Get-FirstMatch $log "error (C|LNK|MSB)\d+|Error:"
        return (New-Bad ("exit $code. $err  ($log)"))
    }
    if ($fatal) { return (New-Bad $fatal) }
    $warn = @(Select-String -Path $log -Pattern "warning C\d+" ).Count
    return (New-Ok "$Target built, $warn compiler warning line(s)")
}

function Step-Tests([string[]]$Filters) {
    Wait-MachineFree
    $log = Join-Path $LogDir "tests.log"
    $out = Join-Path $LogDir "tests.console.txt"
    $report = Join-Path $GateDir "report-tests"
    $filter = ($Filters -join "+")
    $code = Invoke-ToLog "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'run-tests.ps1')`" -Filter `"$filter`" -LogFile `"$log`" -Report `"$report`" -Engine `"$(Get-EngineRoot)`"" $out
    $fatal = Test-FatalLog @($log)
    $summary = Get-FirstMatch $out "^Hawkeye tests \("
    if ($fatal) { return (New-Bad $fatal) }
    if ($code -ne 0) { return (New-Bad ("exit $code. " + $(if ($summary) { $summary } else { Get-LogTail $out 8 }) + "  ($out)")) }
    return (New-Ok ($summary -replace "^Hawkeye tests \(.*\): ", ""))
}

function Step-Content([string[]]$Titles, [string]$Tag = "content") {
    Wait-MachineFree
    $log = Join-Path $LogDir "$Tag.txt"
    $only = ""
    if ($Titles -and -not ($Titles.Count -eq 1 -and $Titles[0] -eq "all")) { $only = " -Only `"$($Titles -join ',')`"" }
    $before = Get-ContentStatus
    $code = Invoke-ToLog "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'create-content.ps1')`" -Engine `"$(Get-EngineRoot)`"$only" $log
    $engineLog = Join-Path $Root "Saved\Logs\HawkeyeContent.log"
    $fatal = Test-FatalLog @($engineLog)
    if ($fatal) { return (New-Bad $fatal) }
    if ($code -ne 0) { return (New-Bad ("exit $code. " + (Get-FirstMatch $log "Error|Traceback") + "  ($log)")) }
    $dirty = Compare-ContentStatus $before (Get-ContentStatus)
    if ($dirty) { return (New-Bad ("the run $dirty (the committed assets and the scripts disagree)")) }
    $what = if ($only) { "steps: $($Titles -join ', ')" } else { "all steps" }
    return (New-Ok "$what, nothing left to commit")
}

function Invoke-EditorPythonScript([string]$ScriptFile, [string]$LogName, [string]$PassPattern) {
    Wait-MachineFree
    $cmd = Get-HawkeyeEditorCmd -EngineRoot (Get-EngineRoot)
    $log = Join-Path $LogDir $LogName
    if (Test-Path $log) { Remove-Item $log -Force }
    $script = Join-Path $PSScriptRoot "Editor\$ScriptFile"
    $argList = @("`"$Proj`"", "-run=pythonscript", "-script=`"$script`"", "-unattended", "-nullrhi", "-nosplash", "-nop4", "-stdout",
        "-FullStdOutLogOutput", "-NoLogTimes", "-abslog=`"$log`"")
    $p = Start-Process -FilePath $cmd -ArgumentList $argList -PassThru -WindowStyle Hidden
    if (-not $p.WaitForExit(20 * 60 * 1000)) { & taskkill /PID $p.Id /T /F | Out-Null; return (New-Bad "timed out after 20 min (killed our own process)") }
    $fatal = Test-FatalLog @($log)
    if ($fatal) { return (New-Bad $fatal) }
    if (-not (Test-Path $log)) { return (New-Bad "no log produced") }
    $line = Get-FirstMatch $log $PassPattern
    if (-not $line) { return (New-Bad ("no PASS line. " + (Get-FirstMatch $log "FAIL|Error") + "  ($log)")) }
    return (New-Ok ($line -replace "^.*\] ", ""))
}

function Step-ContentVerify {
    return (Invoke-EditorPythonScript "verify_content.py" "verify_content.log" "==== PASS")
}

function Step-CityVerify {
    Wait-MachineFree
    $log = Join-Path $LogDir "city.txt"
    $before = Get-ContentStatus
    $code = Invoke-ToLog "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'generate-city.ps1')`" -Verify -Engine `"$(Get-EngineRoot)`"" $log
    $fatal = Test-FatalLog @((Join-Path $Root "Saved\Logs\HawkeyeCity.log"), (Join-Path $Root "Saved\Logs\HawkeyeCityVerify.log"))
    if ($fatal) { return (New-Bad $fatal) }
    if ($code -ne 0) { return (New-Bad ("exit $code. " + (Get-FirstMatch $log "FAIL|Error|reported errors") + "  ($log)")) }
    $dirty = Compare-ContentStatus $before (Get-ContentStatus)
    if ($dirty) { return (New-Bad ("the run $dirty (regenerate, then commit the map)")) }
    return (New-Ok ((Get-FirstMatch (Join-Path $Root "Saved\Logs\HawkeyeCityVerify.log") "verify_city PASS") -replace "^.*\] ", ""))
}

function Step-Audit([bool]$CheckNumbers) {
    Wait-MachineFree
    $log = Join-Path $LogDir "audit.txt"
    $engineLog = Join-Path $Root "Saved\Logs\HawkeyeTraversalAudit.log"
    $code = Invoke-ToLog "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'traversal-audit.ps1')`" -Engine `"$(Get-EngineRoot)`"" $log
    $fatal = Test-FatalLog @($engineLog)
    if ($fatal) { return (New-Bad $fatal) }
    if ($code -ne 0) { return (New-Bad ("exit $code. " + (Get-FirstMatch $log "FAILED|Error") + "  ($log)")) }
    $line = Get-FirstMatch $engineLog "Traversal audit: \d+ anchors"
    $anchors = 0; $pairs = 0
    if ($line -match "(\d+) anchors .*?(\d+) reachable;") { $anchors = [int]$Matches[1]; $pairs = [int]$Matches[2] }
    if ($CheckNumbers) {
        $exp = $RulesData.expectedAudit
        if ($anchors -ne [int]$exp.anchors -or $pairs -ne [int]$exp.pairs) {
            return (New-Bad ("exit 0 but $anchors anchors / $pairs pairs, expected $($exp.anchors) / $($exp.pairs) (Tools\Gate\rules.json, expectedAudit)"))
        }
    }
    return (New-Ok "$anchors anchors, $pairs reachable pairs")
}

# One standalone-game process: a lap, or the traversal strips.
function Invoke-GameProcess([string]$TestName, [string]$Tag, [string[]]$Extra, [int]$TimeoutMinutes) {
    Wait-MachineFree
    $cmd = Get-HawkeyeEditorCmd -EngineRoot (Get-EngineRoot)
    $log = Join-Path $LogDir "$Tag.log"
    $report = Join-Path $GateDir "report-$Tag"
    foreach ($f in @($log)) { if (Test-Path $f) { Remove-Item $f -Force } }
    if (Test-Path $report) { Remove-Item $report -Recurse -Force }
    $argList = @("`"$Proj`"", "-game", "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-nosplash", "-log", "-Difficulty=Normal",
        "-abslog=`"$log`"", "-ReportExportPath=`"$report`"", "-ExecCmds=`"Automation RunTests $TestName; Quit`"") + $Extra
    $p = Start-Process -FilePath $cmd -ArgumentList $argList -PassThru
    if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        & taskkill /PID $p.Id /T /F | Out-Null
        return (New-Bad "timed out after $TimeoutMinutes min (killed our own process)")
    }
    $fatal = Test-FatalLog @($log)
    if ($fatal) { return (New-Bad $fatal) }
    if (-not (Test-Path $log)) { return (New-Bad "no log produced") }
    $text = Get-Content $log -Raw
    if ($text -match "Test Completed\. Result=\{(Fail|Failed)\}") {
        return (New-Bad ("test failed: " + (Get-FirstMatch $log "Error:.*Hawkeye|LogAutomationController: Error") + "  ($log)"))
    }
    if ($text -match "Skipped \(no RHI\)") { return (New-Bad "the body skipped itself (no renderer); the window must be open and not minimised") }
    if ($text -notmatch "Test Completed\. Result=\{(Success|Passed)\}") { return (New-Bad "no passing result in the log ($log)") }
    return (New-Ok "")
}

function Step-Lap([string]$Lap, [bool]$RetryIfFlaky) {
    $name = "Hawkeye.Lap.$Lap"
    $r = Invoke-GameProcess $name "lap_$Lap" @() 12
    if (-not $r.Ok -and $RetryIfFlaky -and (@($RulesData.flakyLaps) -contains $Lap)) {
        Write-Host "  $Lap is a known flaky lap ($($r.Detail)); running it once more"
        $r2 = Invoke-GameProcess $name "lap_${Lap}_retry" @() 12
        if ($r2.Ok) { return (New-Ok "passed on the re-run (first run: $($r.Detail))") }
        return (New-Bad ("failed twice. second: " + $r2.Detail))
    }
    return $r
}

function Step-Strips([string[]]$Names) {
    $only = "-HawkeyeTraversalOnly=" + ($Names -join "+")
    $r = Invoke-GameProcess "Hawkeye.Screenshot.Traversal" "strips" @($only) 20
    if ($r.Ok) { return (New-Ok "frames in Saved\Screenshots\Traversal; read them") }
    return $r
}

function Complete-Run([string]$Mode, [string]$FromLabel, [int]$FileCount) {
    $total = ((Get-Date) - $script:Began).TotalSeconds
    $failed = @($script:Results | Where-Object { -not $_.ok })
    # Fresh timings feed the next plan's estimates.
    $t = @{}
    $old = Read-JsonFile $TimingsFile
    if ($old) { $old.PSObject.Properties | ForEach-Object { $t[$_.Name] = $_.Value } }
    foreach ($k in $script:Timings.Keys) { $t[$k] = $script:Timings[$k] }
    ($t | ConvertTo-Json) | Set-Content -Path $TimingsFile -Encoding UTF8

    Write-Host ""
    if ($failed.Count -eq 0) {
        $dirty = @(Get-GitLines @("status", "--porcelain", "--untracked-files=no")).Count -gt 0
        $rec = [ordered]@{
            commit = (& git rev-parse HEAD).Trim(); tier = $Mode; time = (Get-Date).ToString("s"); seconds = [int]$total
            base = $FromLabel; dirty = $dirty; steps = @($script:Results | ForEach-Object { [ordered]@{ id = $_.id; label = $_.label; ok = $_.ok; seconds = $_.seconds; detail = $_.detail } })
        }
        $json = $rec | ConvertTo-Json -Depth 5
        Set-Content -Path $LastPass -Value $json -Encoding UTF8
        if ($Mode -eq "full") { Set-Content -Path $LastFull -Value $json -Encoding UTF8 }
        Write-Host ("GATE {0}: PASS in {1}  ({2} files since {3}, {4} step(s))" -f $Mode, (Format-Seconds $total), $FileCount, $FromLabel, $script:Results.Count)
        exit 0
    }
    Write-Host ("GATE {0}: FAIL in {1}  failed: {2}" -f $Mode, (Format-Seconds $total), (($failed | ForEach-Object { $_.label }) -join "; "))
    exit 1
}

# ---- quick tier

if ($Tier -eq "quick") {
    $from = Resolve-Base
    $fromLabel = Get-ShortCommit $from
    $changed = Get-ChangedFiles $from
    $sel = Get-Selection $changed
    Show-Plan $sel $sel.steps $fromLabel "quick"
    Write-Host ""
    foreach ($s in $sel.steps) {
        switch -Wildcard ($s.id) {
            "python*" { Invoke-GateStep $s.id $s.label { Step-Python @($sel.pythonSuites) } }
            "build" { Invoke-GateStep $s.id $s.label { Step-Build "HawkeyeEditor" } }
            "suite" { Invoke-GateStep $s.id $s.label { Step-Tests @("Hawkeye") } }
            "tests" { Invoke-GateStep $s.id $s.label { Step-Tests @($sel.tests) } }
            "content" { Invoke-GateStep $s.id $s.label { Step-Content @($sel.contentSteps) } }
            "contentVerify" { Invoke-GateStep $s.id $s.label { Step-ContentVerify } }
            "cityVerify" { Invoke-GateStep $s.id $s.label { Step-CityVerify } }
            "audit" { Invoke-GateStep $s.id $s.label { Step-Audit $false } }
            "strips" { Invoke-GateStep $s.id $s.label { Step-Strips @($sel.strips) } }
            "lap:*" { $lap = $s.id.Substring(4); Invoke-GateStep $s.id $s.label { Step-Lap $lap $true } }
        }
        # A failed build makes every engine step meaningless: stop there.
        if ($s.id -eq "build" -and @($script:Results | Where-Object { $_.id -eq "build" -and -not $_.ok }).Count -gt 0) { $script:Halted = $true }
    }
    if (@($sel.steps).Count -eq 0) { Write-Host "Nothing to check for these changes." }
    Complete-Run "quick" $fromLabel @($changed).Count
}

# ---- full tier: the old gate, everything, rarely

Show-Plan $null (Get-FullSteps) "everything" "full"
Write-Host ""
$fromLabel = "everything"
Invoke-GateStep "python" "python tests (all suites) and ruff" { Step-Python @($RulesData.pythonSuites) }
Invoke-GateStep "build" "build HawkeyeEditor" { Step-Build "HawkeyeEditor" }
if (@($script:Results | Where-Object { $_.id -eq "build" -and -not $_.ok }).Count -gt 0) { $script:Halted = $true }
Invoke-GateStep "buildGame" "build Hawkeye (game target)" { Step-Build "Hawkeye" }
if (@($script:Results | Where-Object { $_.id -eq "buildGame" -and -not $_.ok }).Count -gt 0) { $script:Halted = $true }
Invoke-GateStep "suite" "automation suite (whole)" { Step-Tests @("Hawkeye") }
Invoke-GateStep "content1" "create-content, first run" { Step-Content @("all") "content1" }
Invoke-GateStep "content2" "create-content, second run (clean)" { Step-Content @("all") "content2" }
Invoke-GateStep "contentVerify" "verify_content" { Step-ContentVerify }
Invoke-GateStep "cityVerify" "city generate and verify" { Step-CityVerify }
Invoke-GateStep "audit" "traversal audit (expected anchors and pairs)" { Step-Audit $true }
foreach ($lap in @($RulesData.fullLaps)) {
    $l = $lap
    Invoke-GateStep "lap:$l" "lap $l" { Step-Lap $l $true }
}
Complete-Run "full" $fromLabel 0
