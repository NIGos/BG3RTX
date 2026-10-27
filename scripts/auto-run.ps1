# Unattended capture loop: build, start BG3 on its most recent save, let the
# layer take traces by itself once gameplay frames are stable, close the game
# and analyse every new capture.
#   .\scripts\auto-run.ps1                          one trace
#   .\scripts\auto-run.ps1 -Traces 3                three, auto_interval presents apart
#   .\scripts\auto-run.ps1 -Set dump_images=0       extra bg3rtx.ini overrides for this run
#   .\scripts\auto-run.ps1 -Set preset=ultra        a settings preset (common/settings.h) for this run
#   .\scripts\auto-run.ps1 -UserIni <file>          also layer a launcher-written bg3rtx_user.ini
#   .\scripts\auto-run.ps1 -KeepRunning             leave the game open afterwards
#
# Concurrency (docs/TEST_SCENES.md, "Concurrent runs"): runs are "exclusive" (nothing
# else of ours runs meanwhile) unless -Shared: then a -Background -Save run may share the
# GPU with one other such game, from any checkout.
# Timings are only meaningful from exclusive runs; the run mode is printed and written
# to each capture's run.json. -Tag lets two runs share one checkout (scene-sweep.ps1,
# scripts/qa): their files get the tag (captures\auto.<tag>.ini, bg3rtx.<tag>.log,
# captures\<stamp>-<tag>, ...).
param(
  [int]$Traces = 1,
  [int]$TimeoutSec = 480,
  [string[]]$Set = @(),
  [switch]$NoBuild,
  [switch]$KeepRunning,
  # Real presentation instead of headless (needed for frame generation and for
  # anything that must reach the display). Always exclusive.
  [switch]$RealPresent,
  # Hold a camera-pan key (default D) once gameplay is reached, so the
  # captured frame is in motion (tests reprojection, ghosting, DLSS/FG).
  [switch]$Move,
  [byte]$MoveScan = 0x20,
  # With -Move: release the key after this many seconds (0 = hold until the end),
  # so later traces see a static camera at the end of the pan (A/B at a new view).
  [int]$MoveSec = 0,
  # Run under the Khronos validation layer; messages go to captures/validation.log.
  [switch]$Validation,
  # Load this save instead of the newest: a substring of its folder name, for
  # example "Rivington - 74h". See docs/TEST_SCENES.md.
  [string]$Save = "",
  # Leave the PC to the user: the game runs borderless (no desktop mode change),
  # never takes the foreground, and believes it has focus (layer/background.cpp).
  # Keys for -Move are posted to the game window instead of typed globally.
  [switch]$Background,
  # A bg3rtx_user.ini to layer between bg3rtx.ini and this run's overrides (as
  # the launcher writes it). Default: none, so test runs ignore the player's file.
  [string]$UserIni = "",
  # Skip analyze_trace.py on the new captures (the QA harness reads dumps.json only).
  [switch]$NoAnalyze,
  # Timing / performance runs: no other game of ours runs at the same time (the default
  # unless -Shared).
  [switch]$Exclusive,
  # Opt in to a shared run: up to two -Background -Save games of ours at once (docs/
  # TEST_SCENES.md, "Concurrent runs"; experimental: scenes failed to load in pairs).
  [switch]$Shared,
  # Run files of this checkout carry this tag, so two runs can share the checkout.
  [ValidatePattern('^[A-Za-z0-9_]{0,16}$')]
  [string]$Tag = ""
)
$ErrorActionPreference = "Stop"
# "powershell -File" passes "-Set a=1,b=2" as one string: split it.
$Set = @($Set | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$root = Split-Path $PSScriptRoot
$caps = Join-Path $root "captures"
New-Item -ItemType Directory -Force $caps | Out-Null
. (Join-Path $PSScriptRoot "gamelock.ps1")

if (-not $NoBuild) { & (Join-Path $PSScriptRoot "build.ps1") }

# This run's files: untagged as before, or with ".<tag>" (the layer reads BG3RTX_RUN_TAG).
function Tagged([string]$dir, [string]$name, [string]$ext) {
  if ($Tag) { Join-Path $dir "$name.$Tag$ext" } else { Join-Path $dir "$name$ext" }
}
$ini = Tagged $caps "auto" ".ini"
$doneFile = Tagged $caps "auto" ".done"
$log = Tagged $root "bg3rtx" ".log"
$pidFile = Tagged $caps "game" ".pid"
$saveLog = Tagged $caps "saves" ".txt"

# One run per checkout and tag: two would share the files above.
$sha = [System.Security.Cryptography.SHA1]::Create()
$rootKey = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())) | Select-Object -First 8 | ForEach-Object { $_.ToString("x2") })
$checkoutMutex = New-NamedMutex "Global\BG3RTX_checkout_${rootKey}_$Tag"
if (-not (Lock-Mutex $checkoutMutex 0)) {
  Write-Host "Another run of this checkout$(if ($Tag) { " (tag $Tag)" }) is active; waiting..."
  [void](Lock-Mutex $checkoutMutex)
}

# Exclusive: frame generation and anything presented for real needs the foreground,
# foreground runs move the focus, and timings must not share the GPU.
# Shared runs need -Save: -continueGame would pick the newest save of whichever
# profile the game gets (see the Debug_Client_Profile_1 note below).
$isExclusive = -not $Shared -or $Exclusive -or $RealPresent -or -not $Background -or -not $Save
Write-Host "Waiting for the game lock ($(if ($isExclusive) { 'exclusive' } else { 'shared' }))..."
$lock = if ($isExclusive) { Enter-ExclusiveLock } else { Enter-SharedLock (Join-Path $PSScriptRoot "gamelock.ps1") }
$runMode = $lock.Mode
$slot = $lock.Slot
$lockedAt = Get-Date
Write-Host "Run mode: $runMode$(if ($slot -ge 0) { " (slot $slot): timings are not exclusive" }) (lock at $($lockedAt.ToString("o")))"
$game = $null
$cleanedUp = $false
try {

Add-Type -Namespace BG3RTX -Name Win -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
"@ -ErrorAction SilentlyContinue
# With the display asleep Windows throttles presentation to a few frames per
# second, which makes every measurement meaningless: keep it on (and wake it).
[void][BG3RTX.Win]::SetThreadExecutionState([uint32]2147483651)   # CONTINUOUS | SYSTEM_REQUIRED | DISPLAY_REQUIRED
if (-not $Background) {
  [BG3RTX.Win]::mouse_event(0x0001, 1, 0, 0, [UIntPtr]::Zero)
  [BG3RTX.Win]::mouse_event(0x0001, -1, 0, 0, [UIntPtr]::Zero)
}

# Only this run's game (by PID) is ever focused, sent keys, or closed.
function Get-GameWindow {
  if (-not $game -or $game.HasExited) { return [IntPtr]::Zero }
  $game.Refresh()
  $game.MainWindowHandle
}

# BG3 renders but stops presenting while its window is not in the foreground.
# Background runs: the new game window takes the foreground when it is
# created; hand it back to whatever the user had in front.
$userFg = [BG3RTX.Win]::GetForegroundWindow()
function Focus-Game {
  $h = Get-GameWindow
  if ($h -eq [IntPtr]::Zero) { return }
  if ($Background) {
    if ([BG3RTX.Win]::GetForegroundWindow() -eq $h -and $userFg -ne [IntPtr]::Zero) {
      [BG3RTX.Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)   # Alt tap lifts the foreground lock
      [BG3RTX.Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
      [void][BG3RTX.Win]::SetForegroundWindow($userFg)
    }
    return
  }
  if ([BG3RTX.Win]::GetForegroundWindow() -eq $h) { return }
  [BG3RTX.Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)   # Alt tap lifts the foreground lock
  [BG3RTX.Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [void][BG3RTX.Win]::ShowWindow($h, 9)
  [void][BG3RTX.Win]::SetForegroundWindow($h)
}

# The -Move key: typed globally when the game is in front, posted to its
# window in background runs (WM_KEYDOWN/WM_KEYUP carry the scan code; SDL reads it).
$moveVk = 0x44   # D, for the default scan code 0x20
function Send-MoveKey([bool]$down, [bool]$repeat = $false) {
  if (-not $Background) {
    [BG3RTX.Win]::keybd_event(0, $MoveScan, $(if ($down) { 0x0008 } else { 0x000A }), [UIntPtr]::Zero)
    return
  }
  $h = Get-GameWindow
  if ($h -eq [IntPtr]::Zero) { return }
  $l = 1 -bor ([int64]$MoveScan -shl 16)
  if (-not $down) { $l = $l -bor 0xC0000000 }
  elseif ($repeat) { $l = $l -bor 0x40000000 }   # auto-repeat: the key was already down
  [void][BG3RTX.Win]::PostMessage($h, $(if ($down) { 0x0100 } else { 0x0101 }), [IntPtr]$moveVk, [IntPtr]$l)
}

# A game this checkout (and tag) left running: an earlier run with -KeepRunning, or
# one whose script was killed. Other games, other agents' and the player's, stay.
if (Test-Path $pidFile) {
  $old = try { Get-Content $pidFile -Raw | ConvertFrom-Json } catch { $null }
  if ($old -and (Test-ProcAlive $old.pid $old.start)) {
    $p = Get-Process -Id $old.pid
    Write-Host "Closing the game an earlier run of this checkout left running (pid $($p.Id))"
    [void]$p.CloseMainWindow()
    if (-not $p.WaitForExit(20000)) { $p | Stop-Process -Force; $p.WaitForExit(15000) | Out-Null }
  }
  Remove-Item $pidFile -ErrorAction SilentlyContinue
}
$others = @(Get-Process bg3, bg3_dx11 -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "Other games running (left alone): pid $(($others | ForEach-Object Id) -join ', ')" }

# Only once the game lock is held: a game that exited while we waited may have
# rewritten graphicSettings.lsx without its DLSS entries.
$ensure = @((Join-Path $PSScriptRoot "ensure-settings.py"),
            (Join-Path $root "..\..\..\scripts\ensure-settings.py")) | Where-Object { Test-Path $_ } | Select-Object -First 1
$ensureHere = Join-Path $PSScriptRoot "ensure-settings.py"
Repair-DisplaySettings $ensureHere (Join-Path $caps "display_prev.json")
if ($ensure) { py -3 -I $ensure }

# This run's settings: the layer reads the repository's bg3rtx.ini, then this
# override file (%BG3RTX_INI%) with the auto-trace keys and -Set. -Set preset=ultra
# expands that preset here; explicit keys of this file (headless, fg=0, other -Set
# keys) still win. The player's bg3rtx_user.ini is skipped unless -UserIni.
$lines = @("# auto-run.ps1 overrides, layered over bg3rtx.ini ($runMode run)") + @("auto_trace=1", "auto_traces=$Traces") +
         @(if (-not $RealPresent) { "headless=1", "fg=0" }) +   # headless test runs: no frame generation unless asked
         @(if ($Save) { "auto_min_draws=400", "auto_after_load=1" }) + $Set   # other scenes draw far fewer than the foundry (Rivington ~1240)
$lines | Set-Content $ini
$env:BG3RTX_INI = $ini
if ($UserIni) { $env:BG3RTX_USER_INI = (Resolve-Path $UserIni).Path } else { $env:BG3RTX_USER_INI = "none" }
if ($Tag) { $env:BG3RTX_RUN_TAG = $Tag } else { Remove-Item Env:BG3RTX_RUN_TAG -ErrorAction SilentlyContinue }
Remove-Item $doneFile -ErrorAction SilentlyContinue

Remove-Item Env:BG3RTX_BACKGROUND -ErrorAction SilentlyContinue
$displayUser = Join-Path $script:DisplayUsers "$PID.json"
if ($Background) {
  # Borderless at the desktop resolution; the player's values come back after the
  # last background run (gamelock.ps1, Enter-/Exit-BackgroundDisplay).
  Enter-BackgroundDisplay $ensureHere $displayUser
  $env:BG3RTX_BACKGROUND = "1"
}

# -continueGame loads the newest save, by the save time stored inside each
# package (file timestamps are ignored). To load another one, the layer hides
# every other save folder from the game's listing of Savegames\Story
# (layer/savefilter.cpp, BG3RTX_SAVE_ONLY); nothing on disk changes, so Steam
# Cloud sees no difference. Saves this game writes during the run (autosaves) are
# recorded by the layer (BG3RTX_SAVE_LOG) and moved into Savegames\_bg3rtx_parked,
# so the player's list is unaffected; saves of other games are never moved.
$saveRoot = Join-Path $env:LOCALAPPDATA "Larian Studios\Baldur's Gate 3\PlayerProfiles\Public\Savegames\Story"
$savesBefore = @(Get-ChildItem $saveRoot -Directory | ForEach-Object Name)
Remove-Item $saveLog -ErrorAction SilentlyContinue
$env:BG3RTX_SAVE_LOG = $saveLog
$gameArgs = @("-continueGame")
Remove-Item Env:BG3RTX_SAVE_ONLY -ErrorAction SilentlyContinue
if ($Save) {
  $sd = Get-ChildItem $saveRoot -Directory | Where-Object { $_.Name -like "*__*$Save*" } | Select-Object -First 1
  if (-not $sd) { throw "No save matching '$Save'" }
  Write-Host "Save: $($sd.Name)"
  $env:BG3RTX_SAVE_ONLY = $sd.Name
  $gameArgs = @("-loadSaveGame", "`"$($sd.Name)`"")
}
# Shared runs: this game's Script Extender bridge files go to bg3rtx\slot<N>\
# (layer/sebridge.cpp se_token, mods/BG3RTX_Bridge), so two games never mix.
if ($slot -ge 0) { $gameArgs += "bg3rtxse=slot$slot" }
# A game started while another BG3 runs becomes a local "debug client": it uses the
# profile PlayerProfiles\Debug_Client_Profile_1, which has no saves, so it stays in
# the main menu. Shared runs therefore also put a copy of the save there (once; it is
# refreshed when the player's save is newer). The player's own folder is only read.
if ($slot -ge 0) {
  $dbgStory = Join-Path (Split-Path (Split-Path (Split-Path $saveRoot))) "Debug_Client_Profile_1\Savegames\Story"
  $dst = Join-Path $dbgStory $sd.Name
  $srcLsv = Get-ChildItem -LiteralPath $sd.FullName -Filter *.lsv | Select-Object -First 1
  $dstLsv = if (Test-Path -LiteralPath $dst) { Get-ChildItem -LiteralPath $dst -Filter *.lsv | Select-Object -First 1 }
  if (-not $dstLsv -or $dstLsv.Length -ne $srcLsv.Length -or $dstLsv.LastWriteTimeUtc -lt $srcLsv.LastWriteTimeUtc) {
    New-Item -ItemType Directory -Force $dst | Out-Null
    Get-ChildItem -LiteralPath $sd.FullName -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dst -Force }
    Write-Host "Save copied for a second game instance: $dst"
  }
}
function Restore-Saves {
  $parked = Join-Path (Split-Path $saveRoot) "_bg3rtx_parked"
  $mine = @(if (Test-Path $saveLog) { Get-Content $saveLog -Encoding UTF8 | Where-Object { $_ } | Sort-Object -Unique })
  foreach ($name in $mine) {
    $d = Join-Path $saveRoot $name
    if ($savesBefore -contains $name -or -not (Test-Path -LiteralPath $d)) { continue }
    New-Item -ItemType Directory -Force $parked | Out-Null
    Write-Host "Parking save written during the run: $name"
    Move-Item -LiteralPath $d -Destination $parked
  }
  $foreign = @(Get-ChildItem $saveRoot -Directory | Where-Object { $savesBefore -notcontains $_.Name -and $mine -notcontains $_.Name })
  if ($foreign.Count) { Write-Host "New saves not written by this run's game (left in place): $(($foreign | ForEach-Object Name) -join ', ')" }
}

$start = Get-Date
$launchArgs = @{ Clean = $true; GameArgs = $gameArgs }
if ($Validation) {
  $vlog = Tagged $caps "validation" ".log"
  Remove-Item $vlog -ErrorAction SilentlyContinue
  $settings = Tagged $caps "vk_layer_settings" ".txt"
  @("khronos_validation.log_filename = $($vlog.Replace('\','/'))",
    "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG",
    "khronos_validation.report_flags = error,warn",
    "khronos_validation.duplicate_message_limit = 3") | Set-Content $settings
  $env:VK_LAYER_SETTINGS_PATH = $settings
  $launchArgs.Validation = $true
}
$game = @(& (Join-Path $PSScriptRoot "launch-bg3.ps1") @launchArgs | Where-Object { $_ -is [System.Diagnostics.Process] })[0]
if (-not $game) { throw "launch-bg3.ps1 returned no process" }
@{ pid = $game.Id; start = (Get-ProcStart $game); tag = $Tag; mode = $runMode; slot = $slot } | ConvertTo-Json | Set-Content $pidFile
if ($Background) { Set-DisplayUserGame $displayUser $game }
Write-Host "Game pid $($game.Id)"

$deadline = $start.AddSeconds($TimeoutSec)
$lastNote = Get-Date
$result = "timeout"
$moving = $false
$moveDone = $false
$moveStart = Get-Date
while ((Get-Date) -lt $deadline) {
  if ($moving -and $Background) {
    # A posted key-down is a single press to the game; a held key is what
    # keyboard auto-repeat delivers: key-downs at ~30 Hz with the repeat bit.
    for ($i = 0; $i -lt 60; ++$i) { Send-MoveKey $true $true; Start-Sleep -Milliseconds 33 }
  } else {
    Start-Sleep -Seconds 2
  }
  Focus-Game
  if ($Move -and (Test-Path $log)) {
    $last = Get-Content $log -Tail 40 | Select-String '(\d+) draws in the last frame' | Select-Object -Last 1
    if ((Get-Item $log).LastWriteTime -lt $start) { $last = $null }
    if (-not $moveDone -and $last -and [int]$last.Matches[0].Groups[1].Value -gt 2000) {
      if (-not $moving) { Write-Host "gameplay reached: holding scan code 0x$('{0:X2}' -f $MoveScan)"; $moveStart = Get-Date }
      $moving = $true
    }
    if ($moving -and $MoveSec -gt 0 -and ((Get-Date) - $moveStart).TotalSeconds -ge $MoveSec) {
      Send-MoveKey $false   # release: the camera stays where the pan ended
      Write-Host "pan stopped after $MoveSec s"
      $moving = $false
      $moveDone = $true
    }
    if ($moving) { Send-MoveKey $true }
  }
  if (Test-Path $doneFile) { $result = "done"; break }
  if ($game.HasExited) { $result = "game exited"; break }
  # Stall: the game is alive but has stopped rendering ("[watch] presents 0"
  # for 30 s). Seen in background runs, mostly with no foreground window
  # (display off or session locked). End the run early instead of waiting
  # for the timeout, and log the session state for diagnosis.
  if ((Test-Path $log) -and ((Get-Date) - $start).TotalSeconds -gt 60) {
    $w = @(Get-Content $log -Tail 30 | Select-String '\[watch\] presents (\d+)' | Select-Object -Last 6)
    if ($w.Count -ge 6 -and -not ($w | Where-Object { [int]$_.Matches[0].Groups[1].Value -gt 0 })) {
      $fgw = [BG3RTX.Win]::GetForegroundWindow()
      $locked = [bool](Get-Process LogonUI -ErrorAction SilentlyContinue)
      Write-Host "STALL: no presents for 30 s (foreground window $fgw, LogonUI running: $locked)"
      Add-Content (Join-Path $caps "stalls.log") "$(Get-Date -Format s) $root save='$Save' foreground=$fgw logonui=$locked"
      $result = "stalled"
      break
    }
  }
  if (((Get-Date) - $lastNote).TotalSeconds -ge 30) {
    $lastNote = Get-Date
    $tail = if (Test-Path $log) { (Get-Content $log -Tail 1) } else { "(no log yet)" }
    $fg = [BG3RTX.Win]::GetForegroundWindow()
    $owner = Get-Process | Where-Object { $_.MainWindowHandle -eq $fg } | Select-Object -First 1 -ExpandProperty ProcessName
    Write-Host "$([int]((Get-Date) - $start).TotalSeconds)s [foreground: $owner]: $tail"
  }
}
if ($moving) { Send-MoveKey $false }
$end = Get-Date
Write-Host "Run ended: $result after $([int]($end - $start).TotalSeconds)s ($runMode)"

} finally {
  # Also on errors and Ctrl+C. A killed script skips this: its locks are released by
  # Windows, the next run of this checkout closes its game (game.pid), and the next
  # background run restores the display settings once no run of ours is alive.
  if (-not $KeepRunning) {
    # Ask nicely first so the game can save its pipeline cache; a killed game
    # recompiles ~30k pipelines on the next start.
    if ($game -and -not $game.HasExited) {
      [void]$game.CloseMainWindow()
      if (-not $game.WaitForExit(20000)) { $game | Stop-Process -Force; $game.WaitForExit(15000) | Out-Null }
    }
    Remove-Item $pidFile -ErrorAction SilentlyContinue
    if (Get-Command Restore-Saves -ErrorAction SilentlyContinue) { Restore-Saves }
  } elseif ($Save) {
    Write-Host "KeepRunning: saves written during the run are not parked"
  }
  if ($Background -and $displayUser) { Exit-BackgroundDisplay $ensureHere $displayUser ([bool]$KeepRunning) }
  Exit-GameLock $lock
  $checkoutMutex.ReleaseMutex()
  Write-Host "Game lock held $([int]((Get-Date) - $lockedAt).TotalSeconds)s (released at $((Get-Date).ToString("o")))"
}

$capRe = if ($Tag) { "^\d{8}-\d{6}-$Tag$" } else { '^\d{8}-\d{6}$' }
$new = Get-ChildItem $caps -Directory | Where-Object { $_.Name -match $capRe -and $_.LastWriteTime -ge $start -and (Test-Path (Join-Path $_.FullName "done.txt")) } | Sort-Object Name
foreach ($c in $new) {
  # Whether timings in this capture can be trusted (only exclusive runs).
  @{ mode = $runMode; slot = $slot; exclusive = ($runMode -eq "exclusive"); tag = $Tag; save = $Save; game_pid = $game.Id
     run_start = $start.ToString("o"); run_end = $end.ToString("o") } | ConvertTo-Json | Set-Content (Join-Path $c.FullName "run.json")
  if ($NoAnalyze) { Write-Host "capture: $($c.FullName)"; continue }
  py -3 (Join-Path $PSScriptRoot "analyze_trace.py") $c.FullName --disasm
  $geo = Join-Path $c.FullName "geo\report.json"
  if (Test-Path $geo) {
    $g = Get-Content $geo -Raw | ConvertFrom-Json
    Write-Host ("geo: {0}/{1} draws captured, {2} triangles, depth coverage {3:P2} (in front {4:P2}, behind {5:P2}, missing {6:P2}), normals |dot| median {7}" -f
      $g.draws_captured, $g.draws, $g.triangles, $g.depth.coverage, ($g.depth.in_front / $g.depth.geometry),
      ($g.depth.behind / $g.depth.geometry), ($g.depth.missing / $g.depth.geometry), $g.normals.abs_dot_median)
  }
}
if ($runMode -ne "exclusive" -and -not $NoAnalyze) { Write-Host "Note: shared run - frame timings above are not exclusive (another game may have used the GPU)" }
if (-not $new) {
  Write-Host "No completed capture. Last log lines:"
  if (Test-Path $log) { Get-Content $log -Tail 25 }
  exit 1
}
