# Game locks for test runs: up to two background games at once (docs/TEST_SCENES.md,
# "Concurrent runs"). Dot-sourced by auto-run.ps1; "-Holder" runs the lock holder.
#
# Named mutexes (all Global\, so every checkout and worktree sees them; a mutex whose
# owner dies is released by Windows, so a crashed run never leaves a lock behind):
#   BG3RTX_game_run          the exclusive lock. Older copies of auto-run.ps1 take only
#                            this one, so it is held whenever any game of ours runs.
#   BG3RTX_game_slot0/1      one per concurrent (shared) game.
#   BG3RTX_game_excl_wait    held by an exclusive run while it waits for the exclusive
#                            lock: shared runs do not start new games meanwhile, so a
#                            sweep cannot starve it.
#   BG3RTX_game_admin        guards the hand-over between shared runs and the holder.
#   BG3RTX_game_holder       owned by the holder process for its lifetime.
# Event BG3RTX_game_shared_open (manual reset): set while the holder owns the exclusive
# lock on behalf of the shared runs.
#
# Exclusive run (-RealPresent, -Exclusive, foreground runs): excl_wait, then the
# exclusive lock, then both slots. Nothing else of ours runs meanwhile.
# Shared run (-Background headless): a free slot, while the holder keeps the exclusive
# lock. A mutex belongs to the thread that took it, and two shared runs cannot both
# own the exclusive lock, so a small separate process (the holder) takes it for them:
# the first shared run starts it; it releases the lock a few seconds after the last
# slot became free (or at once when an exclusive run is waiting and no slot is in use).
# -Holder -WhilePid <pid>: a session holder (scene-sweep.ps1, scripts/qa) keeps the
# exclusive lock for as long as that process lives, so every game of the session runs
# under it, plus the usual idle time afterwards.
param([switch]$Holder, [int]$WhilePid = 0)

# BG3RTX_LOCK_PREFIX: other names, only for testing this file without touching real runs.
$script:LockPrefix = if ($env:BG3RTX_LOCK_PREFIX) { $env:BG3RTX_LOCK_PREFIX } else { "Global\BG3RTX_game" }
$script:LockNames = @{
  Excl = "${script:LockPrefix}_run"; ExclWait = "${script:LockPrefix}_excl_wait"
  Admin = "${script:LockPrefix}_admin"; Holder = "${script:LockPrefix}_holder"
  Slots = @("${script:LockPrefix}_slot0", "${script:LockPrefix}_slot1"); Open = "${script:LockPrefix}_shared_open"
}

function New-NamedMutex([string]$name) { New-Object System.Threading.Mutex($false, $name) }

# WaitOne that counts an abandoned mutex (its owner died) as acquired.
function Lock-Mutex($m, [int]$ms = -1) {
  try { return $m.WaitOne($ms) } catch [System.Threading.AbandonedMutexException] { return $true }
}

# True when nobody owns it (taken and released at once).
function Test-MutexFree($m) {
  if (Lock-Mutex $m 0) { $m.ReleaseMutex(); return $true }
  return $false
}

function Open-SharedEvent {
  New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $script:LockNames.Open)
}

# Exclusive: returns @{ Mode = "exclusive"; Slot = -1; Held = <mutexes to release> }.
function Enter-ExclusiveLock {
  $n = $script:LockNames
  $wait = New-NamedMutex $n.ExclWait
  [void](Lock-Mutex $wait)
  $excl = New-NamedMutex $n.Excl
  [void](Lock-Mutex $excl)
  $wait.ReleaseMutex()
  $held = @($excl)
  foreach ($s in $n.Slots) { $m = New-NamedMutex $s; [void](Lock-Mutex $m); $held += $m }
  @{ Mode = "exclusive"; Slot = -1; Held = $held }
}

# Shared: returns @{ Mode = "shared"; Slot = 0|1; Held = @(<slot mutex>) }.
function Enter-SharedLock([string]$holderScript) {
  $n = $script:LockNames
  $wait = New-NamedMutex $n.ExclWait
  $admin = New-NamedMutex $n.Admin
  $holderM = New-NamedMutex $n.Holder
  $open = Open-SharedEvent
  $slots = @($n.Slots | ForEach-Object { New-NamedMutex $_ })
  $noted = $false
  while ($true) {
    # An exclusive run is waiting: let it go first.
    [void](Lock-Mutex $wait); $wait.ReleaseMutex()
    [void](Lock-Mutex $admin)
    $holderAlive = -not (Test-MutexFree $holderM)
    if (-not $holderAlive) {
      $open.Reset() | Out-Null   # a holder that died left it set: it no longer owns the lock
      Start-Process powershell -WindowStyle Hidden -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$holderScript`"", "-Holder") | Out-Null
    } elseif ($open.WaitOne(0)) {
      for ($i = 0; $i -lt $slots.Count; ++$i) {
        if (Lock-Mutex $slots[$i] 0) {
          $admin.ReleaseMutex()
          return @{ Mode = "shared"; Slot = $i; Held = @($slots[$i]) }
        }
      }
    }
    $admin.ReleaseMutex()
    if (-not $noted) { Write-Host "Waiting for a game slot (shared run)..."; $noted = $true }
    [void]$open.WaitOne(2000)
    Start-Sleep -Milliseconds 500
  }
}

# For a sweep: starts a holder that keeps the exclusive lock while this process lives,
# and returns once that holder has it. Every game of the sweep then runs under the
# exclusive lock, and no older script's run can start (and kill games) meanwhile.
function Start-SharedSession([string]$holderScript) {
  Start-Process powershell -WindowStyle Hidden -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
    "`"$holderScript`"", "-Holder", "-WhilePid", $PID) | Out-Null
  Write-Host "Waiting for the exclusive lock for this session..."
  $flag = Join-Path $env:TEMP "bg3rtx_session_$PID.ready"
  while (-not (Test-Path $flag)) { Start-Sleep -Milliseconds 500 }
  Remove-Item $flag -ErrorAction SilentlyContinue
  Write-Host "Session holder has the exclusive lock"
}

function Exit-GameLock($lock) {
  if (-not $lock) { return }
  # Slots first, the exclusive lock last.
  $h = @($lock.Held)
  for ($i = $h.Count - 1; $i -ge 0; --$i) { try { $h[$i].ReleaseMutex() } catch { } }
  $lock.Held = @()
}

if ($Holder) {
  $n = $script:LockNames
  $holderM = New-NamedMutex $n.Holder
  if ($WhilePid) { [void](Lock-Mutex $holderM) }      # once any other holder has gone
  elseif (-not (Lock-Mutex $holderM 0)) { return }   # another holder runs
  $owner = if ($WhilePid) { Get-Process -Id $WhilePid -ErrorAction SilentlyContinue } else { $null }
  $excl = New-NamedMutex $n.Excl
  $wait = New-NamedMutex $n.ExclWait
  $admin = New-NamedMutex $n.Admin
  $open = Open-SharedEvent
  $slots = @($n.Slots | ForEach-Object { New-NamedMutex $_ })
  [void](Lock-Mutex $excl)   # waits for older scripts' runs and exclusive runs
  [void](Lock-Mutex $admin); $open.Set() | Out-Null; $admin.ReleaseMutex()
  if ($WhilePid) { Set-Content (Join-Path $env:TEMP "bg3rtx_session_$WhilePid.ready") "ok" }
  $idle = 0
  while ($true) {
    Start-Sleep -Milliseconds 1500
    [void](Lock-Mutex $admin)
    $free = @($slots | Where-Object { Test-MutexFree $_ }).Count -eq $slots.Count
    $idle = if ($free) { $idle + 1 } else { 0 }
    $exclWaiting = -not (Test-MutexFree $wait)
    if ($owner -and -not $owner.HasExited) { $idle = 0; $exclWaiting = $false }   # the session still runs
    # ~6 s of no shared game: long enough for a sweep to start its next scene.
    if ($free -and ($exclWaiting -or $idle -ge 4)) {
      $open.Reset() | Out-Null
      $admin.ReleaseMutex()
      break
    }
    $admin.ReleaseMutex()
  }
  $excl.ReleaseMutex()
  $holderM.ReleaseMutex()
}

# ---- display settings shared by background runs ----
# Background runs need borderless (Fullscreen=0, FakeFullscreenEnabled=1). The player's
# values are saved once, by the first background run, to %LOCALAPPDATA%\BG3RTX\
# display_prev.json, and put back by the last one to finish. Each background run
# registers itself in display_users\<script pid>.json (with its game's PID); an entry
# counts while the script or its game is alive, so a crashed run is pruned and the
# next run restores the player's values before anything else (crash recovery).

$script:DisplayDir = Join-Path $env:LOCALAPPDATA "BG3RTX"
$script:DisplayPrev = Join-Path $script:DisplayDir "display_prev.json"
$script:DisplayUsers = Join-Path $script:DisplayDir "display_users"

function Test-ProcAlive([int]$procId, [string]$start) {
  if (-not $procId) { return $false }
  $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
  if (-not $p) { return $false }
  if (-not $start) { return $true }
  try { return $p.StartTime.ToString("o") -eq $start } catch { return $true }
}

function Get-ProcStart($p) { try { $p.StartTime.ToString("o") } catch { "" } }

function Get-LiveDisplayUsers {
  New-Item -ItemType Directory -Force $script:DisplayUsers | Out-Null
  @(Get-ChildItem $script:DisplayUsers -Filter *.json | ForEach-Object {
    $u = try { Get-Content $_.FullName -Raw | ConvertFrom-Json } catch { $null }
    if ($u -and ((Test-ProcAlive $u.script_pid $u.script_start) -or (Test-ProcAlive $u.game_pid $u.game_start))) { $_.FullName }
    else { Remove-Item $_.FullName -ErrorAction SilentlyContinue }
  })
}

function Invoke-DisplayLocked([scriptblock]$body) {
  $m = New-NamedMutex "Global\BG3RTX_display"
  [void](Lock-Mutex $m)
  try { & $body } finally { $m.ReleaseMutex() }
}

# Restores the player's values when no background run of ours is alive (crash recovery).
# Also takes over a captures\display_prev.json left by an older copy of this script.
function Repair-DisplaySettings([string]$ensure, [string]$legacyPrev) {
  Invoke-DisplayLocked {
    if (@(Get-LiveDisplayUsers).Count) { return }
    foreach ($f in @($script:DisplayPrev, $legacyPrev)) {
      if ($f -and (Test-Path $f)) {
        Write-Host "Restoring display settings left by an interrupted run ($f)"
        py -3 -I $ensure --restore $f
        Remove-Item $f
      }
    }
  }
}

function Enter-BackgroundDisplay([string]$ensure, [string]$userFile, [switch]$Windowed) {
  Invoke-DisplayLocked {
    New-Item -ItemType Directory -Force $script:DisplayDir | Out-Null
    $live = @(Get-LiveDisplayUsers)
    if (-not $live.Count -and (Test-Path $script:DisplayPrev)) {
      Write-Host "Restoring display settings left by an interrupted run"
      py -3 -I $ensure --restore $script:DisplayPrev
      Remove-Item $script:DisplayPrev
    }
    # In an unfocused real-present FG run, desktop-sized borderless can make
    # the swapchain 3840x2160 while native UI/hudless stay 3840x1600. Windowed
    # keeps the game's requested output extent; both settings still restore.
    $displayArgs=@('Fullscreen=0',"FakeFullscreenEnabled=$(if($Windowed){0}else{1})")
    if (Test-Path $script:DisplayPrev) { py -3 -I $ensure --set @displayArgs }
    else { py -3 -I $ensure --set @displayArgs --prev $script:DisplayPrev }
    $me = Get-Process -Id $PID
    @{ script_pid = $PID; script_start = (Get-ProcStart $me); game_pid = 0; game_start = "" } | ConvertTo-Json | Set-Content $userFile
  }
}

function Set-DisplayUserGame([string]$userFile, $game) {
  if (-not (Test-Path $userFile)) { return }
  $u = Get-Content $userFile -Raw | ConvertFrom-Json
  $u.game_pid = $game.Id; $u.game_start = (Get-ProcStart $game)
  $u | ConvertTo-Json | Set-Content $userFile
}

# keepGame: -KeepRunning; the entry stays (alive with the game), so the run that
# finds the game gone restores the values.
function Exit-BackgroundDisplay([string]$ensure, [string]$userFile, [bool]$keepGame) {
  Invoke-DisplayLocked {
    if (-not $keepGame) { Remove-Item $userFile -ErrorAction SilentlyContinue }
    $live = @(Get-LiveDisplayUsers)
    if (-not $live.Count -and (Test-Path $script:DisplayPrev)) {
      py -3 -I $ensure --restore $script:DisplayPrev
      Remove-Item $script:DisplayPrev
    } elseif ($live.Count) {
      Write-Host "Display settings stay borderless: $($live.Count) other background run(s) active"
    }
  }
}
