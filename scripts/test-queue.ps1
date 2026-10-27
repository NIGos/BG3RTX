# Serial in-game test queue: the only process that launches the game.
#
#   .\scripts\test-queue.ps1                 # run every pending job, exit when the queue is empty
#   .\scripts\test-queue.ps1 -IdleExitSec 300   # keep polling, exit after 5 idle minutes
#   .\scripts\test-queue.ps1 -DryRun -Root <dir>   # no game: test-queue-fake.ps1 plays it (offline test)
#
# Jobs are JSON files in captures/queue/pending/ (any agent or session may add one; only
# this runner starts the game). They run oldest first, one game at a time:
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
param([int]$IdleExitSec = 0, [switch]$DryRun, [string]$Root = "")

$ErrorActionPreference = "Stop"
if ($DryRun -and -not $Root) { throw "-DryRun needs -Root <scratch dir> (never the real captures)" }
$root = if ($Root) { (Resolve-Path $Root).Path } else { Split-Path $PSScriptRoot -Parent }
$q = Join-Path $root "captures\queue"
$pending, $running, $done = "pending", "running", "done" | ForEach-Object { Join-Path $q $_ }
New-Item -ItemType Directory -Force $pending, $running, $done | Out-Null
$jsonl = Join-Path $q "results.jsonl"
$md = Join-Path $q "results.md"
# Per-run logs; pruned after 3 days (docs: disk hygiene).
$logs = Join-Path $q "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
Get-ChildItem $logs -File -ErrorAction SilentlyContinue | Where-Object LastWriteTime -lt (Get-Date).AddDays(-3) | Remove-Item -ErrorAction SilentlyContinue
$script:sess = $null

function Stamp($tree) {
  $commit = try { (git -C $tree log --oneline -1 2>$null) -join "" } catch { "" }   # (no git: a dry run's scratch tree)
  $core = Get-Item (Join-Path $tree "bin\bg3rtx_core.dll") -ErrorAction SilentlyContinue
  "$commit | core $(if ($core) { $core.LastWriteTime.ToString('HH:mm:ss') } else { '?' })"
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
  @(Get-ChildItem (Join-Path $tree "captures\aftermath\*.nv-gpudmp") -ErrorAction SilentlyContinue |
    Where-Object LastWriteTime -gt $since | ForEach-Object Name)
}

function Get-JobTree($job) {
  [IO.Path]::GetFullPath($(if ($job.tree) { Join-Path $root $job.tree } else { $root }))
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
    if ($script:sess.proc.HasExited) { return $false }
    Start-Sleep -Milliseconds 250
  }
  -not $script:sess.proc.HasExited
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
    "- session $id did not become ready: $why" | Add-Content $md
    return @{ failed = $why }
  }
  $log = Join-Path $tree "bg3rtx.log"
  $s = @{ id = $id; key = (Get-SessionKey $job); tree = $tree; proc = $proc; out = $out; quit = $quit; request = $request
          log = $log; ini = (Join-Path $caps "auto.ini"); readySec = [int](Get-Content $ready -Raw | ConvertFrom-Json).sec
          runs = 0; layerMtime = (Get-Item $layer).LastWriteTimeUtc
          coreMtime = (Get-Item (Join-Path $tree "bin\bg3rtx_core.dll")).LastWriteTimeUtc
          generation = (Get-LastGeneration $log); scanPos = (Get-LogSize $log) }
  $s.base = [IO.File]::ReadAllText($s.ini)
  "- session ${id}: ready after $($s.readySec)s (core generation $($s.generation), $($job.save))" | Add-Content $md
  $script:sess = $s
  $s
}

function Stop-Session([string]$reason) {
  $s = $script:sess
  if (-not $s) { return }
  $script:sess = $null
  if (-not $s.proc.HasExited) {
    Set-Content $s.quit "quit"
    if (-not $s.proc.WaitForExit(120000)) { Stop-Process -Id $s.proc.Id -Force -ErrorAction SilentlyContinue }
  }
  # A request the game never took would trace the main menu of the next launch.
  Remove-Item $s.request -ErrorAction SilentlyContinue
  $saved = $s.readySec * [Math]::Max(0, $s.runs - 1)
  "- session $($s.id) closed ($reason): $($s.runs) run(s), launch to ready $($s.readySec)s, about ${saved}s saved; $(Get-RunEnded $s.out)" |
    Add-Content $md
}

# One run inside the session: settings, warm-up, traces, slice of the log.
function Invoke-SessionRun($job, $name, [int]$i, $tree, [int]$traces) {
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
  $caps = @()
  $phase = $null
  if (-not $fault) {
    $set = @($job.set) -join ','
    if (-not (Set-SessionIni (Merge-Ini $s.base $set))) { $fault = "settings not re-read (game not presenting?)" }
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
    while (-not (Test-Path (Join-Path $dir "done.txt")) -and (Get-Date) -lt $w) { Start-Sleep -Milliseconds 250 }
    if ($t -lt $traces -and -not (Wait-Session $gap)) { $fault = "game gone between traces" }
  }
  if (-not $fault -and $job.hold -and -not (Wait-Session ([double]$job.hold))) { $fault = "game gone during hold" }
  $to = Get-LogSize $s.log
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
          session = "$($s.id)#$k"; generation = $s.generation; saved = $(if ($k -gt 1) { $s.readySec } else { 0 }); fault = $fault }
  $r   # after a fault the caller closes the session, once the run is recorded
}

# One run of its own: auto-run.ps1 (or the job's command) launches and closes the game.
function Invoke-SingleRun($job, $name, [int]$i, $tree, [int]$traces, [int]$timeout) {
  $t0 = Get-Date
  $lines = @()
  $runner = if ($DryRun) { Join-Path $PSScriptRoot "test-queue-fake.ps1" } else { Join-Path $tree "scripts\auto-run.ps1" }
  $runArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runner,
               "-NoBuild", "-Background", "-NoAnalyze", "-Traces", $traces, "-TimeoutSec", $timeout)
  if ($DryRun) { $runArgs += @("-Root", $tree) }
  if ($job.save) { $runArgs += @("-Save", $job.save) }
  if ($job.set) { $runArgs += @("-Set", (@($job.set) -join ',')) }
  if ($job.args) { $runArgs += @($job.args) }
  $phase = $null
  if ($job.phases) {
    $phase = Start-Process py -ArgumentList "-3", (Join-Args @((Join-Path $tree "scripts\live_phases.py"), (Join-Path $tree $job.phases))) `
      -WorkingDirectory $tree -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $q "$name.$i.phases.out")
  }
  Push-Location $tree
  try {
    # "command" replaces auto-run for tools that launch the game themselves (python -m scripts.qa run ...).
    # Through cmd: a native tool's stderr must not become a terminating PowerShell error.
    $out = if ($job.command) { cmd /c "$($job.command) 2>&1" | ForEach-Object { "$_" } }
           else { & powershell @runArgs 2>&1 | ForEach-Object { "$_" } }
    if ($job.command) { $lines += @($out | Select-Object -Last 8) }
  } finally { Pop-Location }
  if ($phase) { Stop-Process -Id $phase.Id -ErrorAction SilentlyContinue }
  $outcome = ($out | Select-String 'Run ended|STALL' | Select-Object -First 1).Line
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
     session = ""; generation = $null; saved = 0 }
}

function Run-Job($file) {
  $job = Get-Content $file.FullName -Raw | ConvertFrom-Json
  $name = if ($job.name) { $job.name } else { $file.BaseName }
  $tree = Get-JobTree $job
  $repeat = if ($job.repeat) { [int]$job.repeat } else { 1 }
  $traces = if ($null -ne $job.traces) { [int]$job.traces } else { 2 }
  $timeout = if ($job.timeout) { [int]$job.timeout } else { 300 }
  $key = Get-SessionKey $job
  if ($script:sess -and $script:sess.key -ne $key) { Stop-Session "next job differs" }
  "## $name ($(Get-Date -Format 'HH:mm'), $repeat run(s), $($job.save), $(@($job.set) -join ','))$(if ($key) { ' [session]' })" | Add-Content $md
  for ($i = 1; $i -le $repeat; $i++) {
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
                  keptLog = $null; captures = @(); lines = @(); session = "failed"; generation = $null; saved = 0 }
        } elseif (-not $s) {
          "- (layer in this tree has no session support: run alone)" | Add-Content $md
        }
      }
      if (-not $r -and $script:sess) { $r = Invoke-SessionRun $job $name $i $tree $traces }
    }
    if (-not $r) { $r = Invoke-SingleRun $job $name $i $tree $traces $timeout }
    $caps = @($r.captures)
    $lines = @($r.lines)
    if ($job.post) {
      Push-Location $tree
      try { $lines += @(cmd /c "$($job.post.Replace("{captures}", ($caps -join ' '))) 2>&1" | ForEach-Object { "$_" } | Select-Object -Last 6) }
      finally { Pop-Location }
    }
    # "expect": log lines that prove the run tested what it meant to (the feature switched on,
    # the asset DB loaded, the level loaded). A run missing any is INVALID, never a pass.
    $missing = @()
    if ($job.expect -and $r.keptLog -and (Test-Path $r.keptLog)) {
      foreach ($p in @($job.expect | Where-Object { $_ })) {
        if (-not (Select-String -LiteralPath $r.keptLog -Pattern $p -Quiet)) { $missing += $p }
      }
    } elseif ($job.expect) { $missing = @($job.expect) }
    if ($missing) {
      $r.outcome = "INVALID (missing: $($missing -join ' | ')); $($r.outcome)"
      $lines += "INVALID: expected log lines not found: $($missing -join ' | ')"
    }
    $after = Stamp $tree
    $rec = [ordered]@{ job = $name; run = $i; start = $t0.ToString('s'); sec = [int]((Get-Date) - $t0).TotalSeconds
                       valid = (-not $missing)
                       outcome = $r.outcome; nv153 = $r.nv; dumps = @($r.dumps); build = $before
                       build_changed = ($before -ne $after); log = $r.keptLog; captures = $caps; lines = $lines
                       session = $r.session; core_gen = $r.generation; saved_sec = $r.saved }
    ($rec | ConvertTo-Json -Compress -Depth 4) | Add-Content $jsonl
    $flag = if ($before -ne $after) { " BUILD CHANGED" } else { "" }
    $sessText = if ($r.session) { " session=$($r.session) gen=$($r.generation) saved~$($r.saved)s" } else { "" }
    "- run ${i}: $($r.outcome) nv153=$($r.nv)$(if ($r.dumps) { " dump=$($r.dumps -join ',')" })$sessText$flag" | Add-Content $md
    $lines | ForEach-Object { "  - ``$_``" } | Add-Content $md
    if ($r.fault) { Stop-Session "fault: $($r.fault)" }
    if ($job.stop_on_dump -and $r.dumps) { break }
  }
}

# Our games stay hidden while the queue runs (scripts/game-minimizer.ps1).
$minimizer = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass",
  "-File", "`"$(Join-Path $PSScriptRoot "game-minimizer.ps1")`"", "-WhilePid", $PID

# Agents whose sandbox can't write the main queue drop jobs into their own worktree's
# captures/queue/pending/; the runner collects those too (the job's "tree" names the worktree).
function Get-InboxDirs {
  @($pending) + @(if (-not $DryRun) {
    Get-ChildItem -LiteralPath (Join-Path $root ".claude\worktrees") -Directory -Force -ErrorAction SilentlyContinue |
      ForEach-Object { Join-Path $_.FullName "captures\queue\pending" } | Where-Object { Test-Path -LiteralPath $_ }
  })
}

function Get-NextJob {
  Get-InboxDirs | ForEach-Object { Get-ChildItem -LiteralPath $_ -Filter "*.json" -ErrorAction SilentlyContinue } |
    Sort-Object LastWriteTime | Select-Object -First 1
}

# A changed test-queue.ps1 replaces this runner between jobs (no restart by hand).
$selfPath = $PSCommandPath
$selfStamp = (Get-Item -LiteralPath $selfPath).LastWriteTime
function Test-SelfChanged { (Get-Item -LiteralPath $selfPath).LastWriteTime -ne $selfStamp }

$idle = Get-Date
try {
  while ($true) {
    if (-not $DryRun -and -not $script:sess -and (Test-SelfChanged)) {
      "- runner: test-queue.ps1 changed, restarting on the new version" | Add-Content $md
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
      "- job error: $($_.Exception.Message)" | Add-Content $md
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
} finally { Stop-Session "runner stopped" }
"queue empty"
