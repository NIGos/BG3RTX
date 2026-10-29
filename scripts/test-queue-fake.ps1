# Stand-in for auto-run.ps1 plus the game and the layer, for test-queue.ps1 -DryRun:
# no game starts. It takes auto-run's arguments (unknown ones are ignored), writes
# <Root>\captures\auto.ini like auto-run, and plays the layer's side in <Root>\bg3rtx.log:
#   "[cfg] settings read" when auto.ini changes, "[hot] core generation N loaded" when
#   bin\bg3rtx_core.dll changes, "[auto] scene ready" / auto.done after the "load"
#   ($env:BG3RTX_FAKE_LOAD_SEC, default 3), "[trace] frame N written ... -> <dir>" for each
#   trace.request (or for the -Traces auto traces of a single run), "[watch] presents" and a
#   "[core] guard: fake gi=.. rt_reflections=.." line each second from the live settings.
# Faults on request, through a live key: fake_fault=nv (an nv153 event in
# captures\fake_nv153.txt), fake_fault=stall (presents stop: "Run ended: stalled"),
# fake_fault=exit (the game exits).
# -Session follows auto-run's protocol: captures\session.ready, captures\session.quit, -WhilePid.
param([string]$Root, [int]$Traces = 1, [int]$TimeoutSec = 300, [string[]]$Set = @(), [string]$Save = "",
      [switch]$Session, [int]$SessionMaxSec = 7200, [int]$WhilePid = 0,
      [switch]$NoActivate, [switch]$RealPresent, [switch]$Foreground)
$ErrorActionPreference = "Stop"
if (-not $Root) { throw "-Root required" }
$Set = @($Set | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$caps = Join-Path $Root "captures"
New-Item -ItemType Directory -Force $caps | Out-Null
$ini, $doneFile, $readyFile, $quitFile = "auto.ini", "auto.done", "session.ready", "session.quit" | ForEach-Object { Join-Path $caps $_ }
$logPath = Join-Path $Root "bg3rtx.log"
$request = Join-Path $Root "trace.request"
$core = Join-Path $Root "bin\bg3rtx_core.dll"
if ($Session) { $Traces = 0 }
$loadSec = if ($env:BG3RTX_FAKE_LOAD_SEC) { [double]$env:BG3RTX_FAKE_LOAD_SEC } else { 3 }

@("# fake auto-run overrides", "auto_trace=1", "auto_traces=$Traces", "headless=1", "fg=0") +
  @(if ($Save) { "auto_min_draws=400", "auto_after_load=1" }) + $Set | Set-Content $ini
Remove-Item $doneFile, $readyFile, $quitFile -ErrorAction SilentlyContinue

$log = New-Object IO.StreamWriter ([IO.File]::Open($logPath, 'Create', 'Write', 'ReadWrite, Delete'))
function L([string]$msg) { $log.Write("$((Get-Date).ToString('HH:mm:ss.fff'))  $msg`r`n"); $log.Flush() }
function Read-Keys {
  $k = @{}
  foreach ($l in (Get-Content $ini -ErrorAction SilentlyContinue)) { if ($l -match '^\s*([^#=]+?)\s*=\s*(.*)$') { $k[$matches[1]] = $matches[2].Trim() } }
  $k
}

$start = Get-Date
Write-Host "Fake game (dry run): save '$Save', session $([bool]$Session)"
if ($NoActivate) { Write-Host "[fake-window-mode] no_activate=$NoActivate real_present=$RealPresent foreground=$Foreground session=$Session" }
L "[hot] core generation 1 loaded"
$gen = 1
$coreSeen = (Get-Item $core).LastWriteTimeUtc
$coreChange = $null
$iniSeen = (Get-Item $ini).LastWriteTimeUtc
$keys = Read-Keys
L "[cfg] settings read: auto.ini ($($keys.Count) keys) -> $($keys.Count) keys"
$present = 0
$ready = $false
$autoTaken = 0
$recordingAt = -1
$nextWatch = Get-Date
$stalled = $false
$nvDone = $false
$deadline = $start.AddSeconds($TimeoutSec)
$result = "timeout"
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 100
  if (-not $stalled) { $present += 12 }
  $mt = (Get-Item $ini).LastWriteTimeUtc
  if (-not $stalled -and $mt -ne $iniSeen) {
    $iniSeen = $mt
    $keys = Read-Keys
    L "[cfg] settings read: auto.ini ($($keys.Count) keys) -> $($keys.Count) keys"
  }
  switch ($keys["fake_fault"]) {
    "exit" { L "[fake] game exits"; $result = "game exited" }
    "stall" { if (-not $stalled) { L "[fake] presents stop"; $stalled = $true; $stallAt = Get-Date } }
    "nv" { if (-not $nvDone) { Add-Content (Join-Path $caps "fake_nv153.txt") (Get-Date).ToString('o'); $nvDone = $true; L "[fake] nv153" } }
  }
  if ($result -eq "game exited") { break }
  if ($stalled -and ((Get-Date) - $stallAt).TotalSeconds -ge 2) {
    Write-Host "STALL: no presents for 30 s (fake)"
    $result = "stalled"
    break
  }
  if ($stalled) { continue }
  # Hot reload: wait until the file has settled, as layer/hotreload.cpp does.
  $cm = (Get-Item $core).LastWriteTimeUtc
  if ($cm -ne $coreSeen) {
    if (-not $coreChange) { $coreChange = Get-Date }
    elseif (((Get-Date) - $coreChange).TotalMilliseconds -ge 400) {
      $coreSeen = $cm; $coreChange = $null; $gen++
      L "[hot] core generation $gen loaded"
      L "[hot] reload took 120 ms"
    }
  }
  if ((Get-Date) -ge $nextWatch) {
    $nextWatch = (Get-Date).AddSeconds(1)
    L "[watch] presents 120"
    $g = if ($keys.ContainsKey("gi")) { $keys["gi"] } else { "default" }
    $rf = if ($keys.ContainsKey("rt_reflections")) { $keys["rt_reflections"] } else { "default" }
    L "[core] [core] guard: fake gi=$g rt_reflections=$rf gen=$gen"
  }
  if (-not $ready -and ((Get-Date) - $start).TotalSeconds -ge $loadSec) {
    $ready = $true
    if ($Traces -le 0) {
      L "[auto] scene ready at present $present (auto_traces=0: no trace)"
      Set-Content $doneFile "ready"
    }
  }
  $wantTrace = $false
  if ($recordingAt -lt 0 -and (Test-Path $request)) { Remove-Item $request; $wantTrace = $true; $why = "file" }
  if ($recordingAt -lt 0 -and $ready -and $autoTaken -lt $Traces) { $autoTaken++; $wantTrace = $true; $why = "auto" }
  if ($wantTrace) {
    $recordingAt = $present
    L "[trace] recording from present $present ($why, 60 passes last frame)"
  } elseif ($recordingAt -ge 0) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dir = Join-Path $caps $stamp
    while (Test-Path $dir) { Start-Sleep -Milliseconds 200; $dir = Join-Path $caps (Get-Date -Format 'yyyyMMdd-HHmmss') }
    New-Item -ItemType Directory $dir | Out-Null
    L "[trace] frame $($recordingAt + 1) written: 812 submits, 240 images -> $dir"
    Set-Content (Join-Path $dir "done.txt") "ok"
    $recordingAt = -1
    if (-not $Session -and $Traces -gt 0 -and $autoTaken -ge $Traces) {
      L "[auto] all $autoTaken traces taken"
      Set-Content $doneFile "done"
    }
  }
  if ($Session) {
    if ($ready -and (Test-Path $doneFile) -and -not (Test-Path $readyFile)) {
      @{ sec = [int]((Get-Date) - $start).TotalSeconds; pid = $PID } | ConvertTo-Json -Compress | Set-Content $readyFile
      Write-Host "Session ready"
      $deadline = (Get-Date).AddSeconds($SessionMaxSec)
    }
    if (Test-Path $quitFile) { $result = "session closed"; break }
    if ($WhilePid -and -not (Get-Process -Id $WhilePid -ErrorAction SilentlyContinue)) { $result = "session owner exited"; break }
  } elseif (Test-Path $doneFile) { $result = "done"; break }
}
L "[fake] game closed"
$log.Dispose()
Remove-Item $readyFile, $quitFile -ErrorAction SilentlyContinue
Write-Host "Run ended: $result after $([int]((Get-Date) - $start).TotalSeconds)s (fake)"
