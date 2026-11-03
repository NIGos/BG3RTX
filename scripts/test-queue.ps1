# Serial in-game test queue: the only process that launches the game.
#
#   .\scripts\test-queue.ps1                 # run every pending job, exit when the queue is empty
#   .\scripts\test-queue.ps1 -IdleExitSec 300   # keep polling, exit after 5 idle minutes
#   .\scripts\test-queue.ps1 -DryRun -Root <dir>   # no game: test-queue-fake.ps1 plays it (offline test)
#
# Only authorized batches enter captures/queue/pending/. Unapproved drafts stay
# outside every watched pending inbox. This runner is the sole game launch path.
# Jobs run oldest first, one game at a time:
#
#   { "name": "split-guard",                 # file-safe label (default: the file name)
#     "tree": ".claude/worktrees/agent-x",   # checkout whose bin/ and scripts/ are used (default: repo root)
#     "save": "Campsite - 27h",
#     "set": "rt_bvh_split=1,fg=0",          # auto-run -Set (a session job: applied live)
#     "traces": 2, "timeout": 300, "repeat": 8,
#     "args": ["-Validation", "-VSet", "validate_sync=true"],   # extra auto-run arguments
#     "phases": "captures/em_phases.json",   # optional live_phases spec (relative to tree)
#     "patterns": ["\\[core\\] guard:", "\\[em\\] per build"],  # log lines to keep (last match each)
#     "stop_on_dump": true,                  # stop repeating once an Aftermath dump appears
#     "expect": ["\\[assetdb\\] loaded", "sharc: first frame"],  # must appear, else the run is INVALID
#     "expect_every": [{"select": "rt stats \\(", "match": "...", "windows": ["diag-on"]}],
#                                            # EVERY complete selected line in those move-plan windows
#                                            # must match (scripts/queue_gates.py), else INVALID
#     "post": "py -3 scripts/qa/x.py {captures}",               # optional command after each run
#     "command": "py -3 -m scripts.qa run --scenes S6",         # instead of auto-run, for tools
#                                                               # that launch the game themselves
#     "session": true,                       # share one running game (below)
#     "warmup": 10, "trace_gap": 8, "hold": 0 }                 # session timing, seconds
#
# Sessions: consecutive jobs with "session": true and the same tree, save and args share
# one game (auto-run.ps1 -Session), which saves the launch and load (~55 s) of every run
# but the first. Per run, the runner waits for a rebuilt core to hot-reload ("[hot] core
# generation N loaded"), writes the job's "set" into captures\auto.ini (the layer re-reads
# it; "[cfg] settings read" confirms it), waits "warmup" seconds, requests each trace through
# trace.request ("trace_gap" seconds apart), waits "hold" seconds, then restores the session's
# settings. Patterns are matched in the run's own slice of bg3rtx.log (by byte offset).
# The game is closed when the next job cannot use it, when the queue is empty, after a
# fault (nv153, an Aftermath dump, a stall, a lost trace) and when the tree's
# VkLayer_bg3rtx.dll changes (a layer change needs a new process); later jobs relaunch.
# Keys that only act at startup (rt, headless, dlss/fg setup, ...) need "session": false.
#
# Each run appends one record to captures/queue/results.jsonl and a line to
# captures/queue/results.md: outcome, seconds, NVIDIA device-lost events (System log,
# nvlddmkm id 153), new Aftermath dumps, the matched log lines, and the build stamp
# (commit and core DLL time) at start and end, flagged if the build changed mid-run;
# session runs add the session ("s<hhmmss>#<run>"), the core generation and the
# seconds saved (the session's launch-to-ready time, for every run but its first).
param([int]$IdleExitSec = 0, [switch]$DryRun, [string]$Root = "", [string]$PermissionSession = "")

$ErrorActionPreference = "Stop"
if ($DryRun -and -not $Root) { throw "-DryRun needs -Root <scratch dir> (never the real captures)" }
$root = if ($Root) { (Resolve-Path $Root).Path } else { Split-Path $PSScriptRoot -Parent }
. (Join-Path $PSScriptRoot 'queue-permission.ps1')
. (Join-Path $PSScriptRoot 'queue-window-mode.ps1')
# DryRun never touches a checkout or a queue below one, even if manually marked.
if($DryRun){
  if(-not(Test-Path -LiteralPath (Join-Path $root '.queue-fake-root'))){throw 'DryRun requires a marked scratch root outside checkouts'}
  $ancestor=[IO.DirectoryInfo]$root
  while($ancestor){if(Test-Path -LiteralPath (Join-Path $ancestor.FullName '.git')){throw 'DryRun cannot use a checkout or its captures tree'};$ancestor=$ancestor.Parent}
}
$script:permissionRunner="$PID|$([guid]::NewGuid().ToString('N'))"
$script:permissionStates=@()
$script:childContextFiles=@()
$script:minimizer=$null
function Assert-QueueRuntime {
  $playing=Get-QueueUserPlayingBlock $root
  if($playing){throw $playing}
  if($DryRun){if(Test-Path -LiteralPath (Join-Path $root 'fake_unowned_game')){throw 'Unowned game exists'};return}
  foreach($game in @(Get-Process bg3,bg3_dx11 -ErrorAction SilentlyContinue)){
    if(-not $script:sess -or $game.Id -ne $script:sess.gamePid -or $game.StartTime.ToUniversalTime().Ticks -ne $script:sess.gameStart){throw 'An unowned game exists'}
  }
}
if (-not $DryRun) {
  $playing=Get-QueueUserPlayingBlock $root
  if($playing){throw $playing}
}
$env:BG3RTX_QUEUE_ROOT = $root
if (-not $DryRun -and (Get-Process bg3,bg3_dx11 -ErrorAction SilentlyContinue)) {
  throw 'An existing BG3 process is active; do not start another runner'
}
$q = Join-Path $root "captures\queue"
$pending, $running, $done = "pending", "running", "done" | ForEach-Object { Join-Path $q $_ }
New-Item -ItemType Directory -Force $pending, $running, $done | Out-Null
$jsonl = Join-Path $q "results.jsonl"
$md = Join-Path $q "results.md"

# Appends to a results file, retrying while a reader holds it open (an editor, an agent's
# Select-String): a locked file must never stop the queue.
function Write-Record {
  param([Parameter(Mandatory = $true)][string]$Path, [Parameter(ValueFromPipeline = $true)]$Line)
  begin { $buf = New-Object System.Collections.Generic.List[string] }
  process { if ($null -ne $Line) { $buf.Add([string]$Line) } }
  end {
    for ($try = 0; $try -lt 40; $try++) {
      try { [IO.File]::AppendAllLines($Path, $buf); return } catch { Start-Sleep -Milliseconds 250 }
    }
    Write-Warning "could not write $Path after 10 s"
  }
}
# Per-run logs; pruned after 3 days (docs: disk hygiene).
$logs = Join-Path $q "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
Get-ChildItem $logs -File -ErrorAction SilentlyContinue | Where-Object LastWriteTime -lt (Get-Date).AddDays(-3) | Remove-Item -ErrorAction SilentlyContinue
$script:sess = $null

function Assert-QueueActive {
  Assert-QueueRuntime
  $ctx=$script:permissionContext
  if($ctx){$block=Get-QueuePermissionBlock $ctx.job $root $ctx.tree $ctx.build $PermissionSession $script:permissionRunner -InFlight
    if($block){throw $block}}
}

function Stamp($tree) {
  $commit = try { (git -C $tree log --oneline -1 2>$null) -join "" } catch { "" }   # (no git: a dry run's scratch tree)
  $core = Get-Item (Join-Path $tree "bin\bg3rtx_core.dll") -ErrorAction SilentlyContinue
  $layer = Get-Item (Join-Path $tree "bin\VkLayer_bg3rtx.dll") -ErrorAction SilentlyContinue
  "$commit | core $(if ($core) { $core.LastWriteTimeUtc.Ticks } else { '?' }) | layer $(if ($layer) { $layer.LastWriteTimeUtc.Ticks } else { '?' })"
}

function Get-Nv153($tree, [datetime]$since) {
  if ($DryRun) {   # the fake game's stand-in for the System log
    $f = Join-Path $tree "captures\fake_nv153.txt"
    if (-not (Test-Path $f)) { return 0 }
    return @(Get-Content $f | Where-Object { $_ -and [datetime]$_ -ge $since }).Count
  }
  @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'nvlddmkm'; StartTime = $since } -ErrorAction SilentlyContinue).Count
}

function Get-Dumps($tree, [datetime]$since) {
  @(Get-ChildItem -Path (Join-Path $tree "captures\aftermath\*.nv-gpudmp"), (Join-Path $tree 'captures\crash\*.dmp') -ErrorAction SilentlyContinue |
    Where-Object LastWriteTime -gt $since | ForEach-Object Name)
}

# Jobs without a "tree" run on the test tree (scripts/test-tree.ps1: a checkout of main kept
# apart, so merges and builds in the main checkout never touch a build under a running game);
# without one, on the main checkout as before.
function Get-JobTree($job) {
  $default = Join-Path $root ".claude\worktrees\test-main"
  if ($DryRun -or -not (Test-Path (Join-Path $default "bin\VkLayer_bg3rtx.dll"))) { $default = $root }
  [IO.Path]::GetFullPath($(if ($job.tree) { Join-Path $root $job.tree } else { $default }))
}

# Jobs that may share a running game: same tree, save and launch arguments.
function Get-SessionKey($job) {
  if (-not $job.session -or $job.command) { return "" }
  "$(Get-JobTree $job)|$($job.save)|$(@($job.args) -join ' ')".ToLowerInvariant()
}

# Start-Process takes one command line: quote what has spaces.
function Join-Args([string[]]$a) {
  ($a | ForEach-Object { if ($_ -match '[\s"]' -or $_ -eq '') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
}

# bg3rtx.log is open in the game: read it shared, and take its length from the handle
# (a directory listing can report a stale size while the game writes).
function Get-LogSize([string]$path) {
  if (-not (Test-Path $path)) { return [long]0 }
  $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
  try { $fs.Length } finally { $fs.Dispose() }
}

function Read-LogText([string]$path, [long]$from, [long]$to = -1) {
  if (-not (Test-Path $path)) { return "" }
  $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
  try {
    $end = if ($to -ge 0) { [Math]::Min($to, $fs.Length) } else { $fs.Length }
    if ($from -ge $end) { return "" }
    [void]$fs.Seek($from, 'Begin')
    $buf = New-Object byte[] ($end - $from)
    $n = 0
    while ($n -lt $buf.Length) { $r = $fs.Read($buf, $n, $buf.Length - $n); if ($r -le 0) { break }; $n += $r }
    [Text.Encoding]::UTF8.GetString($buf, 0, $n)
  } finally { $fs.Dispose() }
}

# The first session log line after byte $from matching $pattern (a regex Match), or $null
# after $sec seconds or once the session's game is gone.
function Wait-LogLine([string]$pattern, [long]$from, [double]$sec) {
  $s = $script:sess
  $end = (Get-Date).AddSeconds($sec)
  while ($true) {
    Assert-QueueActive
    if (Test-SessionFailFast) { return $null }
    $gone = $s.proc.HasExited
    foreach ($line in ((Read-LogText $s.log $from) -split "`r?`n")) {
      $m = [regex]::Match($line, $pattern)
      if ($m.Success) { return $m }
    }
    if ($gone -or (Get-Date) -ge $end) { return $null }
    Start-Sleep -Milliseconds 250
  }
}

# Sleeps $sec seconds; $false if the session's game went away meanwhile.
function Wait-Session([double]$sec) {
  $end = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $end) {
    Assert-QueueActive
    if (Test-SessionFailFast) { return $false }
    if ($script:sess.proc.HasExited) { return $false }
    Start-Sleep -Milliseconds 250
  }
  -not $script:sess.proc.HasExited
}

function Test-SessionFailFast {
  if (-not $script:sessionEvidence -or $script:sessionEvidence.job.fail_fast -eq $false) { return $false }
  $ctx=$script:sessionEvidence
  $text=Read-LogText $script:sess.log $ctx.from
  foreach($pattern in @($ctx.job.forbid | Where-Object { $_ })) {
    if($text -match $pattern) { $ctx.failure="forbidden log pattern: $pattern"; return $true }
  }
  if($ctx.start -and $env:BG3RTX_QUEUE_CONTEXT_FILE -and (Get-Date) -ge $ctx.nextPoll) {
    $ctx.nextPoll=(Get-Date).AddSeconds(1)
    $python=if(Get-Command py -ErrorAction SilentlyContinue){'py'}else{'python'}
    $pythonArgs=@();if($python -eq 'py'){$pythonArgs+='-3'}
    $guard=Join-Path $PSScriptRoot 'live_guard.py'
    $elapsed=((Get-Date)-$ctx.start).TotalSeconds.ToString('R',[Globalization.CultureInfo]::InvariantCulture)
    try {
      $answer=@(& $python @pythonArgs $guard --context $env:BG3RTX_QUEUE_CONTEXT_FILE --log $script:sess.log --runner ($script:sess.log+'.session-no-plan') --session-elapsed $elapsed --from $ctx.from)
      if($LASTEXITCODE -ne 0){throw 'live guard failed'}
      $failure=($answer -join "`n") | ConvertFrom-Json
      if($failure){$ctx.failure=$failure.check;return $true}
    } catch { $ctx.failure='fail-fast monitor: '+$_.Exception.Message;return $true }
  }
  return $false
}

# The session's auto.ini with the job's "set" keys replacing or adding lines.
function Merge-Ini([string]$base, [string]$set) {
  $kv = @($set -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '=' })
  $keys = @($kv | ForEach-Object { ($_ -split '=', 2)[0].Trim() })
  $lines = @($base -split "`r?`n" | Where-Object { $_ -and $keys -notcontains ($_ -split '=', 2)[0].Trim() })
  (@($lines) + $kv -join "`r`n") + "`r`n"
}

# Writes the session's auto.ini; $true once the layer logged that it re-read it.
function Set-SessionIni([string]$text) {
  Assert-QueueActive
  $s = $script:sess
  $from = Get-LogSize $s.log
  [IO.File]::WriteAllText($s.ini, $text)
  [bool](Wait-LogLine '\[cfg\] settings read' $from 15)
}

function Get-RunEnded([string]$out) {
  $m = Select-String -Path $out -Pattern 'Run ended|STALL' -ErrorAction SilentlyContinue | Select-Object -Last 1
  if ($m) { $m.Line } else { "session process exited" }
}

function Get-LastGeneration([string]$log, [long]$from = 0) {
  $m = [regex]::Matches((Read-LogText $log $from), '\[hot\] core generation (\d+) loaded')
  if ($m.Count) { [int]$m[$m.Count - 1].Groups[1].Value } else { 0 }
}

# Launches auto-run.ps1 -Session for this job's tree and save; returns once the scene is
# ready. $null: the tree's layer predates sessions (no "scene ready" marker), run it alone.
function Start-Session($job, $tree, [int]$timeout) {
  Assert-QueueRuntime
  $caps = Join-Path $tree "captures"
  $layer = Join-Path $tree "bin\VkLayer_bg3rtx.dll"
  if (-not (Select-String -Path $layer -Pattern 'scene ready at present' -SimpleMatch -Quiet)) { return $null }
  $id = "s" + (Get-Date -Format 'HHmmss')
  $ready, $quit = "ready", "quit" | ForEach-Object { Join-Path $caps "session.$_" }
  $request = Join-Path $tree "trace.request"
  Remove-Item $ready, $quit, $request -ErrorAction SilentlyContinue
  $runner = if ($DryRun) { Join-Path $PSScriptRoot "test-queue-fake.ps1" } else { Join-Path $tree "scripts\auto-run.ps1" }
  $a = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runner, "-NoBuild", "-Background", "-NoAnalyze",
         "-Session", "-WhilePid", $PID, "-TimeoutSec", $timeout)
  if ($DryRun) { $a += @("-Root", $tree) }
  if ($job.save) { $a += @("-Save", $job.save) }
  if ($job.args) { $a += @($job.args) }
  $out = Join-Path $q "session.$id.out"
  $t0 = Get-Date
  $proc = Start-Process powershell -ArgumentList (Join-Args $a) -WorkingDirectory $tree -PassThru -NoNewWindow `
    -RedirectStandardOutput $out -RedirectStandardError "$out.err"
  while (-not (Test-Path $ready) -and -not $proc.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $timeout + 60) {
    Start-Sleep -Milliseconds 500
  }
  if (-not (Test-Path $ready)) {
    if (-not $proc.HasExited) {
      Set-Content $quit "quit"
      if (-not $proc.WaitForExit(120000)) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    }
    $why = Get-RunEnded $out
    "- session $id did not become ready: $why" | Write-Record $md
    return @{ failed = $why; completed = $false }
  }
  $log = Join-Path $tree "bg3rtx.log"
  $s = @{ id = $id; key = (Get-SessionKey $job); tree = $tree; proc = $proc; out = $out; quit = $quit; request = $request
          log = $log; ini = (Join-Path $caps "auto.ini"); readySec = [int](Get-Content $ready -Raw | ConvertFrom-Json).sec
          runs = 0; layerMtime = (Get-Item $layer).LastWriteTimeUtc
          coreMtime = (Get-Item (Join-Path $tree "bin\bg3rtx_core.dll")).LastWriteTimeUtc
          generation = (Get-LastGeneration $log); scanPos = (Get-LogSize $log) }
  if(-not $DryRun){$s.gamePid=[int](Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json).pid
    $s.gameStart=(Get-Process -Id $s.gamePid -ErrorAction Stop).StartTime.ToUniversalTime().Ticks}
  $s.base = [IO.File]::ReadAllText($s.ini)
  "- session ${id}: ready after $($s.readySec)s (core generation $($s.generation), $($job.save))" | Write-Record $md
  $script:sess = $s
  $s
}

function Stop-Session([string]$reason) {
  $s = $script:sess
  if (-not $s) { return }
  $script:sess = $null
  if($s.base){[IO.File]::WriteAllText($s.ini,$s.base)}
  if (-not $s.proc.HasExited) {
    Set-Content $s.quit "quit"
    if (-not $s.proc.WaitForExit(120000)) { Stop-Process -Id $s.proc.Id -Force -ErrorAction SilentlyContinue }
  }
  # A request the game never took would trace the main menu of the next launch.
  Remove-Item $s.request -ErrorAction SilentlyContinue
  $saved = $s.readySec * [Math]::Max(0, $s.runs - 1)
  "- session $($s.id) closed ($reason): $($s.runs) run(s), launch to ready $($s.readySec)s, about ${saved}s saved; $(Get-RunEnded $s.out)" |
    Write-Record $md
}

# One run inside the session: settings, warm-up, traces, slice of the log.
function Invoke-SessionRun($job, $name, [int]$i, $tree, [int]$traces) {
  Assert-QueueActive
  $s = $script:sess
  $s.runs++
  $k = $s.runs
  $t0 = Get-Date
  $fault = $null
  $lines = @()
  # A rebuilt core hot-reloads by itself; measure only once its generation is loaded.
  $core = Join-Path $tree "bin\bg3rtx_core.dll"
  $coreNow = (Get-Item $core).LastWriteTimeUtc
  if ($coreNow -ne $s.coreMtime) {
    $m = Wait-LogLine '\[hot\] (core generation (\d+) loaded|core rejected|core faulted)' $s.scanPos 45
    if ($m -and $m.Groups[2].Success) {
      $s.generation = [int]$m.Groups[2].Value
      $s.coreMtime = $coreNow
    } else { $fault = if ($m) { "hot reload: $($m.Value)" } else { "core rebuilt but no new generation loaded" } }
  }
  $s.scanPos = Get-LogSize $s.log
  $from = $s.scanPos
  $script:sessionEvidence=@{job=$job;from=$from;failure=$null;start=$null;nextPoll=[datetime]::MinValue}
  $caps = @()
  $phase = $null
  try {
  if (-not $fault) {
    $set = @($job.set) -join ','
    if (-not (Set-SessionIni (Merge-Ini $s.base $set))) { $fault = "settings not re-read (game not presenting?)" }
    $script:sessionEvidence.start=Get-Date
  }
  if (-not $fault -and $job.phases) {
    $phase = Start-Process py -ArgumentList "-3", (Join-Args @((Join-Path $tree "scripts\live_phases.py"), (Join-Path $tree $job.phases))), "--offset", $from `
      -WorkingDirectory $tree -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $q "$name.$i.phases.out")
  }
  $warmup = if ($null -ne $job.warmup) { [double]$job.warmup } else { 10 }
  $gap = if ($null -ne $job.trace_gap) { [double]$job.trace_gap } else { 8 }
  if (-not $fault -and -not (Wait-Session $warmup)) { $fault = "game gone during warm-up" }
  for ($t = 1; -not $fault -and $t -le $traces; $t++) {
    $at = Get-LogSize $s.log
    Set-Content $s.request "trace"
    $m = Wait-LogLine '\[trace\] frame \d+ written: .* -> (.+?)\s*$' $at 60
    if (-not $m) { $fault = "trace $t not written within 60 s"; break }
    $dir = $m.Groups[1].Value
    $caps += $dir
    $w = (Get-Date).AddSeconds(30)   # dumps and geometry finish after the log line
    while (-not (Test-Path (Join-Path $dir "done.txt")) -and (Get-Date) -lt $w) {
      if (Test-SessionFailFast) { $fault=$script:sessionEvidence.failure; break }
      Start-Sleep -Milliseconds 250
    }
    if ($t -lt $traces -and -not (Wait-Session $gap)) { $fault = "game gone between traces" }
  }
  if (-not $fault -and $job.hold -and -not (Wait-Session ([double]$job.hold))) { $fault = "game gone during hold" }
  $to = Get-LogSize $s.log
  if ($script:sessionEvidence.failure) { $fault=$script:sessionEvidence.failure }
  $script:sessionEvidence=$null # settings restoration must still wait for its acknowledgement
  if ($phase) { Stop-Process -Id $phase.Id -ErrorAction SilentlyContinue }
  Remove-Item $s.request -ErrorAction SilentlyContinue
  if (-not $s.proc.HasExited -and -not (Set-SessionIni $s.base) -and -not $fault) { $fault = "settings not restored" }
  if ($s.proc.HasExited) { $fault = "game: " + ((Get-RunEnded $s.out) -replace '^.*?Run ended:\s*', '') }
  $slice = Read-LogText $s.log $from $to
  $sliceLines = $slice -split "`r?`n"
  foreach ($p in @($job.patterns | Where-Object { $_ })) {
    $hit = $sliceLines | Where-Object { $_ -match $p } | Select-Object -Last 1
    if ($hit) { $lines += $hit.Substring(0, [Math]::Min(600, $hit.Length)) }
  }
  $nv = Get-Nv153 $tree $t0
  $dumps = Get-Dumps $tree $t0
  if (-not $fault -and $nv) { $fault = "nv153" }
  if (-not $fault -and $dumps) { $fault = "aftermath dump" }
  # Every run's log slice is kept: later launches overwrite bg3rtx.log before agents read it.
  $keptLog = Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).log"
  [IO.File]::WriteAllText($keptLog, $slice)
  $sec = [int]((Get-Date) - $t0).TotalSeconds
  $tagText = "session $($s.id)#$k, core gen $($s.generation)"
  $outcome = if ($fault) { "Run ended: $fault after ${sec}s ($tagText)" } else { "Run ended: done after ${sec}s ($tagText)" }
  $r = @{ outcome = $outcome; nv = $nv; dumps = $dumps; keptLog = $keptLog; captures = $caps; lines = $lines
          session = "$($s.id)#$k"; generation = $s.generation; saved = $(if ($k -gt 1) { $s.readySec } else { 0 }); fault = $fault; completed = -not $fault;
          failed_check=$fault; failed_phase=-1; slice_failures=@() }
  $r   # after a fault the caller closes the session, once the run is recorded
  } finally {
    $script:sessionEvidence=$null
    if($phase){Stop-Process -Id $phase.Id -ErrorAction SilentlyContinue}
    if($s.base){[IO.File]::WriteAllText($s.ini,$s.base)}
  }
}

# One run of its own: auto-run.ps1 (or the job's command) launches and closes the game.
function Invoke-SingleRun($job, $name, [int]$i, $tree, [int]$traces, [int]$timeout) {
  $t0 = Get-Date
  $lines = @()
  Assert-QueueRuntime
  $env:BG3RTX_QUEUE_ROOT = $root
  $runner = if ($DryRun) { Join-Path $PSScriptRoot "test-queue-fake.ps1" } else { Join-Path $tree "scripts\auto-run.ps1" }
  $runArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runner,
               "-NoBuild", "-Background", "-NoAnalyze", "-Traces", $traces, "-TimeoutSec", $timeout)
  if ($DryRun) { $runArgs += @("-Root", $tree) }
  if ($job.save) { $runArgs += @("-Save", $job.save) }
  if ($job.set) { $runArgs += @("-Set", (@($job.set) -join ',')) }
  if ($job.args) { $runArgs += @($job.args) }
  $phase = $null; $child=$null; $cancelled=''
  $quit=Join-Path $q ("single."+[guid]::NewGuid().ToString('N')+".quit")
  $previousQuit=$env:BG3RTX_QUEUE_QUIT_FILE
  $env:BG3RTX_QUEUE_QUIT_FILE=$quit
  $runnerLog=Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).runner.txt"
  $stderr=$runnerLog+'.stderr'
  try {
    Assert-QueueActive
    if ($job.phases) {
      $phase = Start-Process py -ArgumentList "-3", (Join-Args @((Join-Path $tree "scripts\live_phases.py"), (Join-Path $tree $job.phases))) `
        -WorkingDirectory $tree -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $q "$name.$i.phases.out")
    }
    Assert-QueueActive
    if($job.command) {
      $child=Start-Process cmd.exe -ArgumentList '/d','/s','/c',('"'+$job.command+' 2>&1"') -WorkingDirectory $tree `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $runnerLog -RedirectStandardError $stderr
    } else {
      $child=Start-Process powershell -ArgumentList (Join-Args $runArgs) -WorkingDirectory $tree `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $runnerLog -RedirectStandardError $stderr
    }
    $deadline=$t0.AddSeconds($timeout)
    while(-not $child.WaitForExit(250)) {
      try {
        # auto-run owns its game and already checks foreign processes. Queue
        # permission checks must not classify that owned single-run game as foreign.
        if($DryRun -and (Test-Path -LiteralPath (Join-Path $root 'fake_unowned_game'))){throw 'Unowned game exists'}
        $ctx=$script:permissionContext
        $block=Get-QueuePermissionBlock $ctx.job $root $ctx.tree $ctx.build $PermissionSession $script:permissionRunner -InFlight
        if($block){throw $block}
        if((Get-Date) -ge $deadline){throw 'Single-run deadline reached'}
      } catch { $cancelled=$_.Exception.Message; break }
    }
    if(-not $cancelled) {
      $ctx=$script:permissionContext
      $cancelled=Get-QueuePermissionBlock $ctx.job $root $ctx.tree $ctx.build $PermissionSession $script:permissionRunner -InFlight
      if(-not $cancelled -and (Get-Date) -ge $deadline){$cancelled='Single-run deadline reached'}
    }
    if($cancelled) {
      if($phase -and -not $phase.HasExited){$phase.Kill();$phase.WaitForExit()}
      [IO.File]::WriteAllText($quit,$cancelled)
      # Normal auto-run cleanup allows 20 s close +15 s owned-process exit.
      # Never enumerate/kill a game here. A non-cooperative command wrapper alone
      # may be stopped; the persistent quit request still guards nested auto-run.
      if(-not $child.WaitForExit(40000)){$child.Kill();$child.WaitForExit()}
    }
    $child.WaitForExit();$childExit=$child.ExitCode
  } finally {
    if($phase -and -not $phase.HasExited){$phase.Kill();$phase.WaitForExit()}
    if($child -and -not $child.HasExited){
      [IO.File]::WriteAllText($quit,'Queue run interrupted')
      if(-not $child.WaitForExit(40000)){$child.Kill();$child.WaitForExit()}
    }
    $env:BG3RTX_QUEUE_QUIT_FILE=$previousQuit
  }
  $out=@(Get-Content -LiteralPath $runnerLog -ErrorAction SilentlyContinue)
  $out+=@(Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue)
  if($cancelled){$out+= "Run ended: cancelled: $cancelled"}
  if ($job.command) { $lines += @($out | Select-Object -Last 8) }
  # Retain phase timestamps and the actual presentation/focus mode for A/B
  # analysis. The ordinary pattern summary keeps only the last matching line.
  $out | Set-Content -LiteralPath (Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).runner.txt")
  $runnerLog = Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).runner.txt"
  $outcome = if($cancelled){"Run ended: cancelled: $cancelled"}else{($out | Select-String -CaseSensitive '^Run ended|^STALL:' | Select-Object -First 1).Line}
  $runnerMissing = @()
  foreach ($pattern in @($job.expect_runner | Where-Object { $_ })) {
    if (-not ($out | Select-String -Pattern $pattern -Quiet)) { $runnerMissing += $pattern }
  }
  $completed = -not $cancelled -and $(if ($job.command) { $childExit -eq 0 } else { $outcome -match '^Run ended: done\b' })
  $statusFile = Join-Path $tree 'captures\run-status.json'
  $statusCopy = $null
  $runStatus = $null
  if ((Test-Path -LiteralPath $statusFile) -and (Get-Item -LiteralPath $statusFile).LastWriteTime -ge $t0) {
    $statusCopy = Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).status.json"
    Copy-Item -LiteralPath $statusFile -Destination $statusCopy
    $runStatus = Get-Content -LiteralPath $statusCopy -Raw | ConvertFrom-Json
    if ($runStatus.PSObject.Properties['completed'] -and -not $runStatus.completed) { $completed = $false }
  }
  $nv = Get-Nv153 $tree $t0
  $log = Get-ChildItem (Join-Path $tree "bg3rtx*.log") -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
  if ($log -and $job.patterns) {
    foreach ($p in $job.patterns) {
      $m = Select-String $log.FullName -Pattern $p | Select-Object -Last 1
      if ($m) { $lines += $m.Line.Substring(0, [Math]::Min(600, $m.Line.Length)) }
    }
  }
  $dumps = Get-Dumps $tree $t0
  $caps = @(Get-ChildItem (Join-Path $tree "captures") -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d{8}-\d{6}$' -and $_.LastWriteTime -gt $t0 } | Sort-Object Name | ForEach-Object FullName)
  $keptLog = $null
  if ($log -and $log.LastWriteTime -gt $t0) {
    $keptLog = Join-Path $logs "$name.$i.$($t0.ToString('HHmmss')).log"
    Copy-Item $log.FullName $keptLog
  }
  @{ outcome = $outcome; nv = $nv; dumps = $dumps; keptLog = $keptLog; captures = $caps; lines = $lines
     session = ""; generation = $null; saved = 0; completed = $completed;
     runner_missing = $runnerMissing; status = $statusCopy; runner_log = $runnerLog; cancelled=[bool]$cancelled;
     failed_check=$runStatus.failed_check; failed_phase=$runStatus.failed_phase; slice_failures=@($runStatus.slice_failures | Where-Object { $null -ne $_ }) }
}

function Run-Job($file) {
  $job = ConvertTo-QueueWindowJob (Get-Content $file.FullName -Raw | ConvertFrom-Json)
  $name = if ($job.name) { $job.name } else { $file.BaseName }
  $tree = Get-JobTree $job
  $repeat = if ($job.repeat) { [int]$job.repeat } else { 1 }
  $traces = if ($null -ne $job.traces) { [int]$job.traces } else { 2 }
  $timeout = if ($job.timeout) { [int]$job.timeout } else { 300 }
  $key = Get-SessionKey $job
  if ($script:sess -and $script:sess.key -ne $key) { Stop-Session "next job differs" }
  "## $name ($(Get-Date -Format 'HH:mm'), $repeat run(s), $($job.save), $(@($job.set) -join ','))$(if ($key) { ' [session]' })" | Write-Record $md
  for ($i = 1; $i -le $repeat; $i++) {
    Assert-QueueRuntime
    $actualBuild=Get-QueueBuildIdentity $tree
    $script:permissionStates+=Use-QueuePermission $job $root $tree $actualBuild $PermissionSession $script:permissionRunner
    $script:permissionContext=@{job=$job;tree=$tree;build=$actualBuild}
    $contextFile=Join-Path $q ('child-context.'+[guid]::NewGuid().ToString('N')+'.json')
    New-QueueChildContext $job $root $tree $actualBuild $PermissionSession $script:permissionRunner | Set-Content -LiteralPath $contextFile -Encoding UTF8
    $script:childContextFiles+=@($contextFile)
    Remove-Item Env:BG3RTX_QUEUE_CONTEXT -ErrorAction SilentlyContinue
    $env:BG3RTX_QUEUE_CONTEXT_FILE=$contextFile
    if(-not $DryRun -and -not $script:minimizer){
      $script:minimizer=Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$(Join-Path $PSScriptRoot 'game-minimizer.ps1')`"",'-WhilePid',$PID
    }
    $t0 = Get-Date
    $before = Stamp $tree
    $r = $null
    if ($key) {
      $s = $script:sess
      if ($s -and (Get-Item (Join-Path $tree "bin\VkLayer_bg3rtx.dll")).LastWriteTimeUtc -ne $s.layerMtime) {
        Stop-Session "VkLayer_bg3rtx.dll rebuilt: needs a new process"
      }
      if ($s -and $s.proc.HasExited) { Stop-Session "game gone between jobs" }
      if (-not $script:sess) {
        $s = Start-Session $job $tree $timeout
        if ($s -and $s.failed) {
          $r = @{ outcome = "Run ended: session did not start: $($s.failed)"; nv = (Get-Nv153 $tree $t0); dumps = (Get-Dumps $tree $t0)
                  keptLog = $null; captures = @(); lines = @(); session = "failed"; generation = $null; saved = 0; completed = $false }
        } elseif (-not $s) {
          "- (layer in this tree has no session support: run alone)" | Write-Record $md
        }
      }
      if (-not $r -and $script:sess) { $r = Invoke-SessionRun $job $name $i $tree $traces }
    }
    if (-not $r) { $r = Invoke-SingleRun $job $name $i $tree $traces $timeout }
    $caps = @($r.captures)
    $lines = @($r.lines)
    if ($job.post -and -not $r.cancelled) {
      Assert-QueueActive
      Push-Location $tree
      try {
        $postCommand = $job.post.Replace('{captures}',($caps -join ' ')).Replace('{log}',[string]$r.keptLog).Replace('{runner}',[string]$r.runner_log).Replace('{status}',[string]$r.status)
        $lines += @(cmd /c "$postCommand 2>&1" | ForEach-Object { "$_" } | Select-Object -Last 6)
        if ($job.post_gate -and $LASTEXITCODE -ne 0) { $r.completed=$false; $r.runner_missing+=@('post-analysis gate passed') }
      }
      finally { Pop-Location }
    }
    # "expect": log lines that prove the run tested what it meant to (the feature switched on,
    # the asset DB loaded, the level loaded). A run missing any is INVALID, never a pass.
    $missing = @()
    if (-not $r.completed) { $missing += 'successful terminal state' }
    if ($r.runner_missing) { $missing += @($r.runner_missing) }
    if ($job.expect -and $r.keptLog -and (Test-Path $r.keptLog)) {
      foreach ($p in @($job.expect | Where-Object { $_ })) {
        if (-not (Select-String -LiteralPath $r.keptLog -Pattern $p -Quiet)) { $missing += $p }
      }
    } elseif ($job.expect) { $missing += @($job.expect) }
    # "expect_every": every selected stats line inside the named move-plan windows must match
    # (scripts/queue_gates.py), so one good window cannot hide bad ones.
    if ($job.expect_every) {
      if ($r.keptLog -and (Test-Path -LiteralPath $r.keptLog)) {
        $gateFile = "$($r.keptLog).expect_every.json"
        ConvertTo-Json -InputObject @($job.expect_every) -Depth 8 | Set-Content -LiteralPath $gateFile -Encoding UTF8
        $gateArgs = @((Join-Path $PSScriptRoot 'queue_gates.py'), '--gates', $gateFile, '--log', $r.keptLog)
        if ($r.runner_log) { $gateArgs += @('--runner', [string]$r.runner_log) }
        $ix = [array]::IndexOf([string[]]@($job.args), '-MovePlan')
        if ($ix -ge 0 -and $ix + 1 -lt @($job.args).Count) { $gateArgs += @('--plan', (Join-Path $tree @($job.args)[$ix + 1])) }
        # A traceback on stderr must make the run INVALID, not stop the runner ("Stop" above).
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        try { $gateOut = @(& py -3 @gateArgs 2>&1 | ForEach-Object { "$_" }); $gateExit = $LASTEXITCODE }
        finally { $ErrorActionPreference = $eap }
        if ($gateExit -ne 0) {
          $missing += @($gateOut | Where-Object { $_ } | Select-Object -First 6)
          if (-not $gateOut) { $missing += 'every-window gates did not run' }
        }
        $lines += @($gateOut | Select-Object -Last 6)
      } else { $missing += 'every-window gates: no kept game log' }
    }
    if ($job.forbid -and $r.keptLog -and (Test-Path -LiteralPath $r.keptLog)) {
      foreach ($pattern in @($job.forbid)) {
        if (Select-String -LiteralPath $r.keptLog -Pattern $pattern -Quiet) { $missing += "forbidden log pattern: $pattern" }
      }
    }
    if((Get-QueueBuildIdentity $tree) -cne $actualBuild){$missing+='frozen candidate build changed'}
    if ($missing) {
      $r.outcome = "INVALID (missing: $($missing -join ' | ')); $($r.outcome)"
      $lines += "INVALID: expected log lines not found: $($missing -join ' | ')"
    }
    $after = Stamp $tree
    $rec = [ordered]@{ job = $name; run = $i; start = $t0.ToString('s'); sec = [int]((Get-Date) - $t0).TotalSeconds
                       valid = (-not $missing)
                       outcome = $r.outcome; nv153 = $r.nv; dumps = @($r.dumps); build = $before
                       build_changed = ($before -ne $after -or (Get-QueueBuildIdentity $tree) -cne $actualBuild); log = $r.keptLog; captures = $caps; lines = $lines
                       session = $r.session; core_gen = $r.generation; saved_sec = $r.saved;
                       process_status = $r.status; runner_log = $r.runner_log; window_mode=$job.window_mode; candidate_build=$actualBuild; live_batch=$job.live_batch; authorization_session=$PermissionSession }
    $rec.completed = [bool]$r.completed
    $rec.failed_check = $r.failed_check
    $rec.failed_phase = $r.failed_phase
    $rec.slice_failures = @($r.slice_failures | Where-Object { $null -ne $_ })
    ($rec | ConvertTo-Json -Compress -Depth 4) | Write-Record $jsonl
    $flag = if ($before -ne $after) { " BUILD CHANGED" } else { "" }
    $sessText = if ($r.session) { " session=$($r.session) gen=$($r.generation) saved~$($r.saved)s" } else { "" }
    "- run ${i}: $($r.outcome) nv153=$($r.nv)$(if ($r.dumps) { " dump=$($r.dumps -join ',')" })$sessText$flag" | Write-Record $md
    $lines | ForEach-Object { "  - ``$_``" } | Write-Record $md
    if ($r.fault) { Stop-Session "fault: $($r.fault)" }
    if ($job.stop_on_dump -and $r.dumps) { break }
  }
}

# Agents whose sandbox can't write the main queue drop jobs into their own worktree's
# captures/queue/pending/; the runner collects those too (the job's "tree" names the worktree).
function Get-InboxDirs {
  @($pending) + @(if (-not $DryRun) {
    Get-ChildItem -LiteralPath (Join-Path $root ".claude\worktrees") -Directory -Force -ErrorAction SilentlyContinue |
      ForEach-Object { Join-Path $_.FullName "captures\queue\pending" } | Where-Object { Test-Path -LiteralPath $_ }
  })
}

$script:blockedQueueMessages=@{}
function Get-QueuePreflightBlock($file, $job) {
  # Non-plan commands have only their deadline as a bound. A session also needs
  # its per-run trace waits (60 s write + 30 s completion), gaps and hold.
  if (@($job.args) -notcontains '-MovePlan') {
    $seconds=if($job.timeout){[double]$job.timeout}else{300.0}
    if($job.session -and -not $job.command) {
      $warmup=if($null -ne $job.warmup){[double]$job.warmup}else{10.0}
      $traces=if($null -ne $job.traces){[double]$job.traces}else{2.0}
      $gap=if($null -ne $job.trace_gap){[double]$job.trace_gap}else{8.0}
      $seconds += $warmup + $traces*90 + [Math]::Max(0,$traces-1)*$gap + [double]$job.hold
    }
    $minutes=if($null -ne $job.max_minutes){[double]$job.max_minutes}else{15.0}
    if([double]::IsNaN($minutes) -or [double]::IsInfinity($minutes) -or $minutes -le 0){return 'live-flow preflight: max_minutes must be finite and positive'}
    if([double]::IsNaN($seconds) -or [double]::IsInfinity($seconds) -or $seconds -le 0){return 'live-flow preflight: duration must be finite and positive'}
    if($seconds -gt $minutes*60){return "live-flow preflight: expected duration ${seconds} s exceeds $minutes minutes; split launch or declare max_minutes"}
    Write-Host "expected duration: at most $seconds s (deadline and session trace/load bounds)"
    return $null
  }
  # MovePlan jobs use the same evidence-backed check as preparation, before
  # consuming a receipt, and lint the exact plan the child will read.
  $python = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
  $pythonArgs = @(); if ($python -eq 'py') { $pythonArgs += '-3' }
  $lint = Join-Path $PSScriptRoot '..\tools\live_batches\live_flow.py'
  $eap = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    $output = @(& $python @pythonArgs $lint $file.FullName --preflight --tree (Get-JobTree $job) 2>&1)
    $exit = $LASTEXITCODE
  } finally { $ErrorActionPreference = $eap }
  if ($exit -ne 0) { return 'live-flow preflight: ' + ($output -join ' | ') }
  foreach ($line in $output) { Write-Host $line }
  return $null
}

function Get-NextJob {
  $candidates=@(Get-InboxDirs | ForEach-Object { Get-ChildItem -LiteralPath $_ -Filter "*.json" -ErrorAction SilentlyContinue } | Sort-Object LastWriteTime)
  $shown=0
  foreach($next in $candidates) {
    try {
      $job = ConvertTo-QueueWindowJob (Get-Content -LiteralPath $next.FullName -Raw | ConvertFrom-Json)
      $tree=Get-JobTree $job
      $blocked=Get-QueuePermissionBlock $job $root $tree (Get-QueueBuildIdentity $tree) $PermissionSession $script:permissionRunner
      if(-not $blocked){$blocked=Get-QueueDependencyBlock $job $root (Stamp $tree)}
      if(-not $blocked){$blocked=Get-QueuePreflightBlock $next $job}
    } catch {$blocked='Invalid queued job: '+$_.Exception.Message}
    if(-not $blocked){return $next}
    # Leave blocked files untouched. At most one message per unchanged reason in
    # each polling minute, with a bounded cache and bounded text.
    $message=($next.Name+': '+$blocked);$key=$next.FullName
    if($message.Length -gt 400){$message=$message.Substring(0,400)}
    $previous=$script:blockedQueueMessages[$key]
    if($shown -lt 4 -and (-not $previous -or $previous.message -ne $message -or ((Get-Date)-$previous.at).TotalSeconds -ge 60)) {
      ++$shown
      Write-Host "Queued, not launched: $message"
      if($script:blockedQueueMessages.Count -ge 128){$script:blockedQueueMessages.Clear()}
      $script:blockedQueueMessages[$key]=@{message=$message;at=Get-Date}
    }
  }
  return $null
}

# A changed test-queue.ps1 replaces this runner between jobs (no restart by hand).
$selfPath = $PSCommandPath
$selfStamp = (Get-Item -LiteralPath $selfPath).LastWriteTime
function Test-SelfChanged { (Get-Item -LiteralPath $selfPath).LastWriteTime -ne $selfStamp }

$idle = Get-Date
try {
  while ($true) {
    if (-not $DryRun -and -not $script:sess -and (Test-SelfChanged)) {
      "- runner: test-queue.ps1 changed, restarting on the new version" | Write-Record $md
      Start-Process powershell -WindowStyle Hidden -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", "`"$selfPath`"", "-IdleExitSec", $IdleExitSec
      break
    }
    $next = Get-NextJob
    if (-not $next) {
      Stop-Session "queue empty"
      if (((Get-Date) - $idle).TotalSeconds -ge $IdleExitSec) { break }
      Start-Sleep -Seconds 10
      continue
    }
    $file = Move-Item $next.FullName (Join-Path $running $next.Name) -PassThru
    try { Run-Job $file }
    catch {
      "- job error: $($_.Exception.Message)" | Write-Record $md
      Stop-Session "job error"
    }
    Move-Item $file.FullName (Join-Path $done $file.Name) -Force
    # Keep the game only for a next job that can use it.
    if ($script:sess) {
      $peek = Get-NextJob
      $peekKey = if ($peek) { try { Get-SessionKey (Get-Content $peek.FullName -Raw | ConvertFrom-Json) } catch { $script:sess.key } }
      if (-not $peek) { Stop-Session "queue empty" }
      elseif ($peekKey -ne $script:sess.key) { Stop-Session "next job differs" }
    }
    $idle = Get-Date
  }
} finally {
  Stop-Session "runner stopped"
  foreach($state in $script:permissionStates | Select-Object -Unique){Close-QueuePermission $state $script:permissionRunner}
  # Session and nested QA children retain their original context until stopped.
  foreach($contextFile in $script:childContextFiles){Remove-Item -LiteralPath $contextFile -ErrorAction SilentlyContinue}
  Remove-Item Env:BG3RTX_QUEUE_CONTEXT_FILE -ErrorAction SilentlyContinue
}
"queue empty"
