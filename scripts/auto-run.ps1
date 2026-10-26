# Unattended capture loop: build, start BG3 on its most recent save, let the
# layer take traces by itself once gameplay frames are stable, close the game
# and analyse every new capture.
#   .\scripts\auto-run.ps1                          one trace
#   .\scripts\auto-run.ps1 -Traces 3                three, auto_interval presents apart
#   .\scripts\auto-run.ps1 -Set dump_images=0       extra bg3rtx.ini overrides for this run
#   .\scripts\auto-run.ps1 -KeepRunning             leave the game open afterwards
param(
  [int]$Traces = 1,
  [int]$TimeoutSec = 480,
  [string[]]$Set = @(),
  [switch]$NoBuild,
  [switch]$KeepRunning,
  # Real presentation instead of headless (needed for frame generation and for
  # anything that must reach the display).
  [switch]$RealPresent,
  # Hold a camera-pan key (default D) once gameplay is reached, so the
  # captured frame is in motion (tests reprojection, ghosting, DLSS/FG).
  [switch]$Move,
  [byte]$MoveScan = 0x20,
  # Run under the Khronos validation layer; messages go to captures/validation.log.
  [switch]$Validation
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$caps = Join-Path $root "captures"
New-Item -ItemType Directory -Force $caps | Out-Null

if (-not $NoBuild) { & (Join-Path $PSScriptRoot "build.ps1") }

# One game at a time across every checkout and worktree of this repository.
$mutex = New-Object System.Threading.Mutex($false, "Global\BG3RTX_game_run")
Write-Host "Waiting for the game lock..."
try { [void]$mutex.WaitOne() } catch [System.Threading.AbandonedMutexException] { }

Add-Type -Namespace BG3RTX -Name Win -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, System.UIntPtr extra);
"@ -ErrorAction SilentlyContinue
# With the display asleep Windows throttles presentation to a few frames per
# second, which makes every measurement meaningless: keep it on (and wake it).
[void][BG3RTX.Win]::SetThreadExecutionState([uint32]2147483651)   # CONTINUOUS | SYSTEM_REQUIRED | DISPLAY_REQUIRED
[BG3RTX.Win]::mouse_event(0x0001, 1, 0, 0, [UIntPtr]::Zero)
[BG3RTX.Win]::mouse_event(0x0001, -1, 0, 0, [UIntPtr]::Zero)
# BG3 renders but stops presenting while its window is not in the foreground.
function Focus-Game {
  $p = Get-Process bg3 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $p) { return }
  $h = $p.MainWindowHandle
  if ([BG3RTX.Win]::GetForegroundWindow() -eq $h) { return }
  [BG3RTX.Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)   # Alt tap lifts the foreground lock
  [BG3RTX.Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [void][BG3RTX.Win]::ShowWindow($h, 9)
  [void][BG3RTX.Win]::SetForegroundWindow($h)
}

Get-Process bg3 -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Host "Closing a running bg3.exe (pid $($_.Id))"
  $_ | Stop-Process -Force
  $_.WaitForExit(15000) | Out-Null
}

# Only once the game lock is held: a game that exited while we waited may have
# rewritten graphicSettings.lsx without its DLSS entries.
$ensure = @((Join-Path $PSScriptRoot "ensure-settings.py"),
            (Join-Path $root "..\..\..\scripts\ensure-settings.py")) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($ensure) { py -3 -I $ensure }

# This run's settings: the repository's bg3rtx.ini plus auto-trace overrides.
$ini = Join-Path $caps "auto.ini"
$lines = @(Get-Content (Join-Path $root "bg3rtx.ini")) + @("auto_trace=1", "auto_traces=$Traces") +
         @(if (-not $RealPresent) { "headless=1", "fg=0" }) + $Set   # headless test runs: no frame generation unless asked
$lines | Set-Content $ini
$env:BG3RTX_INI = $ini
Remove-Item (Join-Path $caps "auto.done") -ErrorAction SilentlyContinue

py -3 -I (Join-Path $PSScriptRoot "ensure-settings.py")
$start = Get-Date
if ($Validation) {
  $vlog = Join-Path $caps "validation.log"
  Remove-Item $vlog -ErrorAction SilentlyContinue
  $settings = Join-Path $caps "vk_layer_settings.txt"
  @("khronos_validation.log_filename = $($vlog.Replace('\','/'))",
    "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG",
    "khronos_validation.report_flags = error,warn",
    "khronos_validation.duplicate_message_limit = 3") | Set-Content $settings
  $env:VK_LAYER_SETTINGS_PATH = $settings
  & (Join-Path $PSScriptRoot "launch-bg3.ps1") -Clean -Validation -GameArgs "-continueGame"
} else {
& (Join-Path $PSScriptRoot "launch-bg3.ps1") -Clean -GameArgs "-continueGame"
}

$log = Join-Path $root "bg3rtx.log"
$deadline = $start.AddSeconds($TimeoutSec)
$lastNote = Get-Date
$result = "timeout"
$moving = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 2
  Focus-Game
  if ($Move -and (Test-Path $log)) {
    $last = Get-Content $log -Tail 40 | Select-String '(\d+) draws in the last frame' | Select-Object -Last 1
    if ((Get-Item $log).LastWriteTime -lt $start) { $last = $null }
    if ($last -and [int]$last.Matches[0].Groups[1].Value -gt 2000) {
      if (-not $moving) { Write-Host "gameplay reached: holding scan code 0x$('{0:X2}' -f $MoveScan)" }
      $moving = $true
    }
    if ($moving) { [BG3RTX.Win]::keybd_event(0, $MoveScan, 0x0008, [UIntPtr]::Zero) }   # KEYEVENTF_SCANCODE down
  }
  if (Test-Path (Join-Path $caps "auto.done")) { $result = "done"; break }
  $p = Get-Process bg3 -ErrorAction SilentlyContinue
  if (-not $p -and ((Get-Date) - $start).TotalSeconds -gt 30) { $result = "game exited"; break }
  if (((Get-Date) - $lastNote).TotalSeconds -ge 30) {
    $lastNote = Get-Date
    $tail = if (Test-Path $log) { (Get-Content $log -Tail 1) } else { "(no log yet)" }
    $fg = [BG3RTX.Win]::GetForegroundWindow()
    $owner = Get-Process | Where-Object { $_.MainWindowHandle -eq $fg } | Select-Object -First 1 -ExpandProperty ProcessName
    Write-Host "$([int]((Get-Date) - $start).TotalSeconds)s [foreground: $owner]: $tail"
  }
}
if ($moving) { [BG3RTX.Win]::keybd_event(0, $MoveScan, 0x000A, [UIntPtr]::Zero) }   # scan-code key up
Write-Host "Run ended: $result after $([int]((Get-Date) - $start).TotalSeconds)s"

if (-not $KeepRunning) {
  # Ask nicely first so the game can save its pipeline cache; a killed game
  # recompiles ~30k pipelines on the next start.
  $p = Get-Process bg3 -ErrorAction SilentlyContinue
  if ($p) {
    [void]$p.CloseMainWindow()
    if (-not $p.WaitForExit(20000)) { $p | Stop-Process -Force }
  }
}
$mutex.ReleaseMutex()

$new = Get-ChildItem $caps -Directory | Where-Object { $_.Name -ne "shaders" -and $_.LastWriteTime -ge $start -and (Test-Path (Join-Path $_.FullName "done.txt")) } | Sort-Object Name
foreach ($c in $new) {
  py -3 (Join-Path $PSScriptRoot "analyze_trace.py") $c.FullName --disasm
  $geo = Join-Path $c.FullName "geo\report.json"
  if (Test-Path $geo) {
    $g = Get-Content $geo -Raw | ConvertFrom-Json
    Write-Host ("geo: {0}/{1} draws captured, {2} triangles, depth coverage {3:P2} (in front {4:P2}, behind {5:P2}, missing {6:P2}), normals |dot| median {7}" -f
      $g.draws_captured, $g.draws, $g.triangles, $g.depth.coverage, ($g.depth.in_front / $g.depth.geometry),
      ($g.depth.behind / $g.depth.geometry), ($g.depth.missing / $g.depth.geometry), $g.normals.abs_dot_median)
  }
}
if (-not $new) {
  Write-Host "No completed capture. Last log lines:"
  if (Test-Path $log) { Get-Content $log -Tail 25 }
  exit 1
}
