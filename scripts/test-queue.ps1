# Serial in-game test queue: the only process that launches the game.
#
#   .\scripts\test-queue.ps1                 # run every pending job, exit when the queue is empty
#   .\scripts\test-queue.ps1 -IdleExitSec 300   # keep polling, exit after 5 idle minutes
#
# Jobs are JSON files in captures/queue/pending/ (any agent or session may add one; only
# this runner starts the game). They run oldest first, one game at a time:
#
#   { "name": "split-guard",                 # file-safe label (default: the file name)
#     "tree": ".claude/worktrees/agent-x",   # checkout whose bin/ and scripts/ are used (default: repo root)
#     "save": "Campsite - 27h",
#     "set": "rt_bvh_split=1,fg=0",          # auto-run -Set
#     "traces": 2, "timeout": 300, "repeat": 8,
#     "args": ["-Validation", "-VSet", "validate_sync=true"],   # extra auto-run arguments
#     "phases": "captures/em_phases.json",   # optional live_phases spec (relative to tree)
#     "patterns": ["\\[core\\] guard:", "\\[em\\] per build"],  # log lines to keep (last match each)
#     "stop_on_dump": true,                  # stop repeating once an Aftermath dump appears
#     "post": "py -3 scripts/qa/x.py {captures}",               # optional command after each run
#     "command": "py -3 -m scripts.qa run --scenes S6" }        # instead of auto-run, for tools
#                                                               # that launch the game themselves
#
# Each run appends one record to captures/queue/results.jsonl and a line to
# captures/queue/results.md: outcome, seconds, NVIDIA device-lost events (System log,
# nvlddmkm id 153), new Aftermath dumps, the matched log lines, and the build stamp
# (commit and core DLL time) at start and end, flagged if the build changed mid-run.
param([int]$IdleExitSec = 0)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$q = Join-Path $root "captures\queue"
$pending, $running, $done = "pending", "running", "done" | ForEach-Object { Join-Path $q $_ }
New-Item -ItemType Directory -Force $pending, $running, $done | Out-Null
$jsonl = Join-Path $q "results.jsonl"
$md = Join-Path $q "results.md"

function Stamp($tree) {
  $commit = (git -C $tree log --oneline -1 2>$null) -join ""
  $core = Get-Item (Join-Path $tree "bin\bg3rtx_core.dll") -ErrorAction SilentlyContinue
  "$commit | core $(if ($core) { $core.LastWriteTime.ToString('HH:mm:ss') } else { '?' })"
}

function Run-Job($file) {
  $job = Get-Content $file.FullName -Raw | ConvertFrom-Json
  $name = if ($job.name) { $job.name } else { $file.BaseName }
  $tree = if ($job.tree) { Join-Path $root $job.tree } else { $root }
  $repeat = if ($job.repeat) { [int]$job.repeat } else { 1 }
  $traces = if ($job.traces) { [int]$job.traces } else { 2 }
  $timeout = if ($job.timeout) { [int]$job.timeout } else { 300 }
  "## $name ($(Get-Date -Format 'HH:mm'), $repeat run(s), $($job.save), $($job.set))" | Add-Content $md
  for ($i = 1; $i -le $repeat; $i++) {
    $t0 = Get-Date
    $before = Stamp $tree
    $lines = @()
    $runArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $tree "scripts\auto-run.ps1"),
                 "-NoBuild", "-Background", "-NoAnalyze", "-Traces", $traces, "-TimeoutSec", $timeout)
    if ($job.save) { $runArgs += @("-Save", $job.save) }
    if ($job.set) { $runArgs += @("-Set", $job.set) }
    if ($job.args) { $runArgs += @($job.args) }
    $phase = $null
    if ($job.phases) {
      $phase = Start-Process py -ArgumentList "-3", (Join-Path $tree "scripts\live_phases.py"), (Join-Path $tree $job.phases) `
        -WorkingDirectory $tree -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $q "$name.$i.phases.out")
    }
    Push-Location $tree
    try {
      # "command" replaces auto-run for tools that launch the game themselves (python -m scripts.qa run ...).
      $out = if ($job.command) { Invoke-Expression "$($job.command) 2>&1" | ForEach-Object { "$_" } }
             else { & powershell @runArgs 2>&1 | ForEach-Object { "$_" } }
      if ($job.command) { $lines += @($out | Select-Object -Last 8) }
    } finally { Pop-Location }
    if ($phase) { Stop-Process -Id $phase.Id -ErrorAction SilentlyContinue }
    $outcome = ($out | Select-String 'Run ended|STALL' | Select-Object -First 1).Line
    $nv = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'nvlddmkm'; StartTime = $t0 } -ErrorAction SilentlyContinue).Count
    $log = Get-ChildItem (Join-Path $tree "bg3rtx*.log") -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($log -and $job.patterns) {
      foreach ($p in $job.patterns) {
        $m = Select-String $log.FullName -Pattern $p | Select-Object -Last 1
        if ($m) { $lines += $m.Line.Substring(0, [Math]::Min(600, $m.Line.Length)) }
      }
    }
    $dumps = @(Get-ChildItem (Join-Path $tree "captures\aftermath\*.nv-gpudmp") -ErrorAction SilentlyContinue |
               Where-Object LastWriteTime -gt $t0 | ForEach-Object Name)
    $caps = @(Get-ChildItem (Join-Path $tree "captures") -Directory -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -match '^\d{8}-\d{6}$' -and $_.LastWriteTime -gt $t0 } | Sort-Object Name | ForEach-Object FullName)
    $keptLog = $null
    if ($log -and ($nv -gt 0 -or $dumps)) {
      $keptLog = Join-Path $q "$name.$i.$($t0.ToString('HHmmss')).log"
      Copy-Item $log.FullName $keptLog
    }
    if ($job.post) {
      Push-Location $tree
      try { $lines += @(Invoke-Expression ($job.post.Replace("{captures}", ($caps -join ' '))) 2>&1 | ForEach-Object { "$_" } | Select-Object -Last 6) }
      finally { Pop-Location }
    }
    $after = Stamp $tree
    $rec = [ordered]@{ job = $name; run = $i; start = $t0.ToString('s'); sec = [int]((Get-Date) - $t0).TotalSeconds
                       outcome = $outcome; nv153 = $nv; dumps = $dumps; build = $before
                       build_changed = ($before -ne $after); log = $keptLog; captures = $caps; lines = $lines }
    ($rec | ConvertTo-Json -Compress -Depth 4) | Add-Content $jsonl
    $flag = if ($before -ne $after) { " BUILD CHANGED" } else { "" }
    "- run ${i}: $outcome nv153=$nv$(if ($dumps) { " dump=$($dumps -join ',')" })$flag" | Add-Content $md
    $lines | ForEach-Object { "  - ``$_``" } | Add-Content $md
    if ($job.stop_on_dump -and $dumps) { break }
  }
}

$idle = Get-Date
while ($true) {
  $next = Get-ChildItem (Join-Path $pending "*.json") -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -First 1
  if (-not $next) {
    if (((Get-Date) - $idle).TotalSeconds -ge $IdleExitSec) { break }
    Start-Sleep -Seconds 10
    continue
  }
  $file = Move-Item $next.FullName (Join-Path $running $next.Name) -PassThru
  try { Run-Job $file }
  catch { "- job error: $($_.Exception.Message)" | Add-Content $md }
  Move-Item $file.FullName (Join-Path $done $file.Name) -Force
  $idle = Get-Date
}
"queue empty"
