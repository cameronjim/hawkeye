# Captures one of the demo video's scripted shots as a 1920x1080, 60 fps frame sequence (Saved\Demo\<shot>\frame_%05d.png).
#
# One shot per engine process, standalone game (-game, a real renderer), with an 8-minute timeout: a shot that
# stalls is killed and reported, never left holding the machine's RAM. See Source\Hawkeye\Tests\DemoCaptureTest.cpp.
#
#   .\Tools\demo-capture.ps1 -Shot grapple_run           # also: descent, lucky, establish
#   .\Tools\demo-capture.ps1 -All                        # each shot in its own process, one after another
#   .\Tools\demo-capture.ps1 -Shot establish -Frames 120 # a short test of the pipeline (120 frames)
#   .\Tools\demo-capture.ps1 -Screen city_generation -Seconds 20   # a desktop recording for the cut (see below)
#
# The process check: this machine has 16 GB of RAM and never runs two editors, so before it starts the script
# runs `tasklist` and refuses when any UnrealEditor process (editor or -Cmd) is up, whoever started it.
# (workflow.md and infrastructure.md say "never run two editor instances"; their check, in play.ps1 and the other
# wrappers, is Get-Process UnrealEditor. This script uses tasklist as the demo plan asked and matches both exes.)
#
# -Screen records the desktop with ffmpeg's gdigrab at 1920x1080, 60 fps, for -Seconds, into
# Saved\Demo\screen\<name>.mp4 (the sources the edit list names: city_generation, traversal_debugger, tests_run,
# lap_bot). Start the thing to be filmed in another window right after this starts; it is untested.

[CmdletBinding(DefaultParameterSetName = "Shot")]
param(
    [Parameter(ParameterSetName = "Shot")]
    [ValidateSet("grapple_run", "descent", "lucky", "establish")]
    [string]$Shot = "",
    [Parameter(ParameterSetName = "All")]
    [switch]$All,
    [Parameter(ParameterSetName = "Screen")]
    [string]$Screen = "",
    [Parameter(ParameterSetName = "Screen")]
    [int]$Seconds = 30,
    # Stop after this many frames (the test's own cap otherwise).
    [int]$Frames = 0,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [int]$TimeoutMinutes = 8,
    [string]$Engine = "",
    [string]$Ffmpeg = "ffmpeg"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$DemoDir = Join-Path $Root "Saved\Demo"

# The test name for each shot (Hawkeye.Demo.<Name>).
$TestNames = @{ grapple_run = "GrappleRun"; descent = "Descent"; lucky = "Lucky"; establish = "Establish" }

function Test-EditorRunning {
    $out = (& tasklist /NH 2>$null) -join "`n"
    return ($out -match "(?im)^UnrealEditor(-Cmd)?\.exe\s")
}

function Invoke-Screen {
    param([string]$Name, [int]$Length)
    $dir = Join-Path $DemoDir "screen"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $file = Join-Path $dir "$Name.mp4"
    Write-Host "Recording the desktop for $Length s into $file ..."
    & $Ffmpeg -y -hide_banner -loglevel error -f gdigrab -framerate 60 -video_size "${ResX}x${ResY}" -i desktop `
        -t $Length -c:v libx264 -preset veryfast -crf 16 -pix_fmt yuv420p $file
    if ($LASTEXITCODE -ne 0) { Write-Error "ffmpeg failed (exit $LASTEXITCODE)"; exit 1 }
    Write-Host "Wrote $file"
}

function Invoke-Shot {
    param([string]$Name)
    . (Join-Path $PSScriptRoot "hawkeye.env.ps1")
    $engineRoot = Resolve-HawkeyeEngine -Explicit $Engine
    $cmd = Get-HawkeyeEditorCmd -EngineRoot $engineRoot
    if (-not (Test-Path $cmd)) { Write-Error "UnrealEditor-Cmd.exe not found at $cmd"; exit 2 }
    if (Test-EditorRunning) {
        Write-Warning "An Unreal process is already running (tasklist). This machine has 16 GB RAM; not starting another."
        exit 1
    }

    $proj = Join-Path $Root "Hawkeye.uproject"
    $log = Join-Path $DemoDir "$Name.log"
    $report = Join-Path $DemoDir "$Name.report"
    New-Item -ItemType Directory -Force $DemoDir | Out-Null
    foreach ($f in @($log)) { if (Test-Path $f) { Remove-Item $f -Force } }
    if (Test-Path $report) { Remove-Item $report -Recurse -Force }

    $test = "Hawkeye.Demo.$($TestNames[$Name])"
    $argList = @("`"$proj`"", "/Game/Maps/L_District_EastVillage", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY",
        "-benchmark", "-fps=60", "-HawkeyeDemo", "-unattended", "-nosplash", "-log", "-abslog=`"$log`"",
        "-ReportExportPath=`"$report`"", "-ExecCmds=`"Automation RunTests $test; Quit`"")
    if ($Frames -gt 0) { $argList += "-DemoFrames=$Frames" }

    Write-Host "[$Name] $test, ${ResX}x${ResY} at 60 fps, timeout $TimeoutMinutes min ..."
    $start = Get-Date
    $proc = Start-Process -FilePath $cmd -ArgumentList $argList -PassThru
    $finished = $proc.WaitForExit($TimeoutMinutes * 60 * 1000)
    if (-not $finished) {
        Write-Warning "[$Name] still running after $TimeoutMinutes min: killing process $($proc.Id) and its children."
        & taskkill /PID $proc.Id /T /F | Out-Null
        return $false
    }
    $wall = [math]::Round(((Get-Date) - $start).TotalSeconds)

    $shotDir = Join-Path $DemoDir $Name
    $count = if (Test-Path $shotDir) { @(Get-ChildItem $shotDir -Filter "frame_*.png").Count } else { 0 }
    $meta = Join-Path $shotDir "shot.json"
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { "" }
    $ok = [regex]::IsMatch($text, "Test Completed\. Result=\{(Success|Passed)\}") -and $count -gt 0 -and (Test-Path $meta)
    Write-Host "[$Name] exit $($proc.ExitCode), $count frames in $shotDir, wall $wall s, $(if ($ok) { 'ok' } else { 'FAILED' }). Log $log"
    if (Test-Path $meta) {
        $json = Get-Content $meta -Raw | ConvertFrom-Json
        Write-Host "[$Name] $($json.frames) frames, $($json.width)x$($json.height), $($json.dropped_frames) dropped, events: $(($json.events | ForEach-Object { "$($_.frame):$($_.event)" }) -join ' ')"
    }
    return $ok
}

if ($PSCmdlet.ParameterSetName -eq "Screen") {
    if (-not $Screen) { Write-Error "Give -Screen a name."; exit 2 }
    Invoke-Screen -Name $Screen -Length $Seconds
    exit 0
}

$shots = if ($All) { @("establish", "grapple_run", "descent", "lucky") } else { @($Shot) }
if (-not $shots[0]) { Write-Error "Give -Shot <grapple_run|descent|lucky|establish>, or -All."; exit 2 }
$failed = @()
foreach ($s in $shots) {
    if (-not (Invoke-Shot -Name $s)) { $failed += $s }
}
if ($failed.Count -gt 0) {
    Write-Host "Failed: $($failed -join ', ')"
    exit 1
}
exit 0
