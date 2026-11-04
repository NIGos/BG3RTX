# Offline check of a lean live batch (tools/live_batches/<Batch>): no game, no real queue.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\lean_dryrun.ps1 -Batch lean_regression [-Out <scratch dir>]
# 1. Shared live_flow duration/preflight checks, then plan sampling, foreground FG 2 single launch,
#    expect_runner names the phase count and every trace, no float bursts, diagnostics
#    (rt_stats, rt_primary_check, rt_primary_identity, ls_stats, ls_check, rt_debug, rf_stats, rf_debug, rt_occ_stats) on
#    exactly in "diag-*" phases, cost windows diag-off and without traces (debug-view traces only in
#    "diag-view-*"). Shared duration totals against -MaxMinutes; disk estimate against -MaxMB.
# 2. Run A (real jobs, fake game): job 1 cannot see its expect lines, so it is INVALID and the
#    dependent jobs stay in pending (stop-on-failure). With -Independent (no requires_success_of):
#    every job runs and every one is INVALID.
# 3. Run B (expect/expect_every/forbid/post stripped): every job runs in order and is valid.
# Scratch root only (outside any checkout); the receipt written there is fake and has no authority.
param([Parameter(Mandatory)][string]$Batch, [string]$Receipt = '',
      [string]$Out = '', [double]$MaxMinutes = 20, [double]$MaxMB = 2048,
      [switch]$Independent, [switch]$Unfocused, [int]$MaxBursts = 0, [double]$BurstMB = 2300, [double]$LoadSec = 120, [double]$TraceMB = 60, [double]$LogMB = 10, [switch]$Force, [string[]]$JobNames = @(), [switch]$ScratchOnly)
# Reuse: a passed check whose inputs (this script, the batch folder, scripts\*.ps1 / *.py, the parameters
# but -Out) are byte-identical to a previous pass prints that pass's lines and exits 0 without the fake
# queue runs (captures\.cache\lean_dryrun-<batch>.json). -Force always reruns.
$ErrorActionPreference = 'Stop'
$here = Join-Path $PSScriptRoot $Batch
if (-not (Test-Path $here)) { throw "no batch folder $here" }
if (-not $Out) { $Out = Join-Path $env:TEMP "bg3rtx-$Batch-dryrun\$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$scripts = Join-Path $repo 'scripts'
. (Join-Path $scripts 'low-priority.ps1')
# Always run before reuse: new VALID logs can expose a false-positive forbid in
# an otherwise unchanged batch. This helper only reads evidence and writes cache.
# The queue gate subprocess also invokes py. Supply one scratch-only adapter when
# this machine has Python but no Windows launcher; child PowerShell inherits PATH.
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  [void](Get-Command python -ErrorAction Stop)
  $pythonShim = Join-Path $Out 'python-shim'
  New-Item -ItemType Directory -Force $pythonShim | Out-Null
  Set-Content -LiteralPath (Join-Path $pythonShim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH = "$pythonShim;$env:PATH"
}
$flowPython = 'py'
$flowArgs = @(); if ($flowPython -eq 'py') { $flowArgs += '-3' }
$flowReport = Join-Path $Out 'live-flow.json'
New-Item -ItemType Directory -Force $Out | Out-Null
$flowSelection = @(); if ($JobNames.Count) { $flowSelection = @('--job-names', ($JobNames -join ',')) }
& $flowPython @flowArgs (Join-Path $PSScriptRoot 'live_flow.py') $here --preflight --report $flowReport --cache (Join-Path $Out 'flow-corpus.json') @flowSelection
if ($LASTEXITCODE -ne 0) { throw 'live-flow design lint failed; fix before fake queue dryrun' }
$flow = Get-Content -LiteralPath $flowReport -Raw | ConvertFrom-Json
$flowDurations = @{}
foreach ($entry in $flow.jobs.PSObject.Properties) {
  $flowJob = Get-Content -LiteralPath $entry.Name -Raw | ConvertFrom-Json
  $flowDurations[$flowJob.name] = $entry.Value.duration
}
. (Join-Path $scripts 'camera-plan.ps1')
. (Join-Path $scripts 'queue-permission.ps1')
. (Join-Path $scripts 'queue-window-mode.ps1')
. (Join-Path $scripts 'longrun.ps1')
if ($ScratchOnly) { function Get-LongrunCacheDir { Join-Path $Out 'cache' } }
$inputs = @($PSCommandPath) + @(Get-ChildItem $here -Recurse -File | ForEach-Object FullName) +
  @(Get-ChildItem $scripts -File -Include '*.ps1', '*.py' -Recurse | ForEach-Object FullName)
$inputHash = Get-LongrunInputHash $repo $inputs ("$Batch|$Receipt|$MaxMinutes|$MaxMB|$Independent|$LoadSec|$TraceMB|$LogMB|$($JobNames -join ',')")
$reuse = if ($Force) { $null } else { Get-LongrunReuse "lean_dryrun-$Batch" $inputHash }
if ($reuse) {
  foreach ($l in @($reuse.lines)) { Write-Host $l }
  Write-Host "reused: inputs unchanged since the pass of $($reuse.utc) UTC (no fake queue runs; -Force reruns)"
  Write-Host 'all checks passed'
  exit 0
}
$fail = 0
$said = New-Object System.Collections.Generic.List[string]
function Say([string]$line) { Write-Host $line; $script:said.Add($line) }
function Check([string]$what, [bool]$ok) { Say ("{0} {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what); if (-not $ok) { $script:fail++ } }
function Apply-Set($state, [string]$set) {
  foreach ($a in ($set -split ',' | Where-Object { $_ })) { $kv = $a -split '=', 2; if ($kv.Count -ne 2) { return $false }; $state[$kv[0]] = $kv[1] }
  $true
}
function Test-Diag($s) { foreach ($k in 'rt_stats', 'rt_primary_check', 'rt_primary_identity', 'ls_stats', 'ls_check', 'rt_debug', 'rf_stats', 'rf_debug', 'rt_occ_stats') { if ($s[$k] -and $s[$k] -ne '0') { return $true } }; $false }

$jobFiles = @(Get-ChildItem (Join-Path $here 'jobs') -Filter '*.json' | Sort-Object Name)
if ($JobNames.Count) { $jobFiles = @($jobFiles | Where-Object { $_.BaseName -in $JobNames }) }
$jobs = @($jobFiles | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
$first = $jobs[0].name
if (-not $Receipt) { $Receipt = "$($jobs[0].live_batch).json" }
Check "$($jobs.Count) job files, all one live_batch ($($jobs[0].live_batch))" ($jobs.Count -ge 1 -and @($jobs.live_batch | Select-Object -Unique).Count -eq 1)
$totalWaitSec = 0; $totalExpectedWaitSec = 0; $totalSec = 0; $totalTraces = 0; $totalBursts = 0
foreach ($job in $jobs) {
  $n = $job.name
  $planRel = $job.args[[array]::IndexOf([string[]]$job.args, '-MovePlan') + 1]
  Check "${n}: plan path is captures/live_batches/$Batch/plans/" ($planRel -like "captures/live_batches/$Batch/plans/*")
  $plan = @(Get-Content (Join-Path $here ('plans\' + (Split-Path $planRel -Leaf))) -Raw | ConvertFrom-Json | ForEach-Object { $_ })
  $budget = $flowDurations[$n]
  if (-not $budget) { throw "live-flow duration missing for $n" }
  $sec = $budget.seconds; $waitSec = $budget.readiness_timeout_seconds; $totalSec += $sec; $totalWaitSec += $waitSec; $totalExpectedWaitSec += $budget.expected_readiness_seconds
  $state = @{}; $ok = Apply-Set $state $job.set
  $sampled = $true; $roles = $true; $bursts = $true; $burstCount = 0; $traceLines = @(); $traceAll = @(); $roleNote = ''
  for ($i = 0; $i -lt $plan.Count; $i++) {
    $step = $plan[$i]
    try { Assert-CameraPlanFields $step } catch { $ok=$false; Say "FAIL ${n}: phase $i $($_.Exception.Message)" }
    if (-not (Apply-Set $state $step.set)) { $ok = $false }
    try { foreach ($t in 0, 1, ($step.seconds / 2), ($step.seconds - 0.5)) { [void](Get-CameraPlanSample $step $t 2560 1067) } } catch { $sampled = $false }
    # -MaxBursts N: a flicker analysis that needs K16 float bursts (scripts/qa/pan_artifacts.py) may take up to N
    # (capture_at and serial both set, counted at BurstMB each in the disk estimate); else none.
    if ($step.serial -and $step.capture_at) { $burstCount++ }
    elseif ($step.serial -or $step.capture_at) { $bursts = $false }
    if ($state['pan_prepare'] -and $state['pan_prepare'] -ne '0' -and $MaxBursts -eq 0) { $bursts = $false }
    $diag = Test-Diag $state; $label = [string]$step.label
    if ($diag -ne $label.StartsWith('diag-')) { $roles = $false; $roleNote += " $label" }
    if ($label.StartsWith('cost-') -and $diag) { $roles = $false }
    if ($step.trace_at) {
      $totalTraces += @($step.trace_at).Count
      if ($label.StartsWith('cost-')) { $roles = $false; $roleNote += " $label(trace in cost window)" }
      if (@($step.trace_at | Where-Object { $_ -ge $step.seconds }).Count) { $ok = $false }
      if ($diag -and -not $label.StartsWith('diag-view-')) { $roles = $false; $roleNote += " $label(trace in diag)" }
      $traceLines += "[motion-trace] phase=$i label=$label repeat=0"
      for ($r = 0; $r -lt @($step.trace_at).Count; $r++) { $traceAll += "[motion-trace] phase=$i label=$label repeat=$r" }
    }
  }
  Check "${n}: plan $(Split-Path $planRel -Leaf) ($($plan.Count) phases, $sec s) parses, sets are key=value, traces inside phases" $ok
  Check "${n}: every phase samples" $sampled
  Check "${n}: diagnostics on exactly in diag-* phases, never in cost windows or with a trace$roleNote" $roles
  if ($burstCount -gt $MaxBursts) { $bursts = $false }
  $totalBursts += $burstCount
  Check "${n}: float bursts $burstCount (allowed $MaxBursts; serial and capture_at both set)" $bursts
  $er = @($job.expect_runner)
  Check "${n}: expect_runner names phases=$($plan.Count), the foreground pointer line and $($traceLines.Count) traces" (
    [bool]($er -match "phases=$($plan.Count)\\b") -and
    [bool]($er -match $(if ($Unfocused) { 'window-mode.*visible_unfocused' } else { 'pointer centred in owned foreground game' })) -and
    -not @($traceLines | Where-Object { $t = $_; -not @($er | Where-Object { $t -match $_ }).Count }).Count)
  # The runner prints "[motion-trace] phase=..." only for phases with trace_at (scripts/auto-run.ps1): an expect
  # for a phase without one never matches (ef1-af-em-fg2 2026-11-02 was INVALID for that).
  $orphan = @($er | Where-Object { $e = $_; $e -match 'motion-trace' -and -not @($traceAll | Where-Object { $_ -match $e }).Count })
  Check "${n}: every expect_runner [motion-trace] line names a phase with trace_at$(if ($orphan.Count) { ': not ' + ($orphan -join ' | ') })" (-not $orphan.Count)
  # -Unfocused: the user is at the PC (AGENTS.md "Lean live tests"): window_mode visible_unfocused_fg, no -Foreground.
  $modeOk = if ($Unfocused) { $job.window_mode -eq 'visible_unfocused_fg' -and @($job.args) -notcontains '-Foreground' }
            else { -not $job.window_mode -and @($job.args) -contains '-RealPresent' -and @($job.args) -contains '-Foreground' }
  Check "${n}: $(if ($Unfocused) { 'unfocused' } else { 'foreground' }) single launch, fg=2, hold >= 90, patterns/expect/forbid present" (
    $modeOk -and
    @($job.args) -contains '-Move' -and $job.set -match '(^|,)fg=2(,|$)' -and -not $job.session -and $job.hold -ge 90 -and
    @($job.patterns).Count -and @($job.expect).Count -and @($job.forbid).Count)
  $norm = ConvertTo-QueueWindowJob $job
  if ($Unfocused) { Check "${n}: queue normalises the unfocused job (window_mode kept)" ([bool]$norm) }
  else { Check "${n}: queue keeps the foreground args (no -NoActivate)" (@($norm.args) -notcontains '-NoActivate' -and (@($norm.args) -join ' ') -eq (@($job.args) -join ' ')) }
  if ($Independent) { Check "${n}: independent (no requires_success_of)" (-not $job.requires_success_of) }
  else { Check "${n}: stop-on-failure dependency" ($n -eq $first -or @($job.requires_success_of) -contains $first) }
}
# Shared job lint: every lean job gates "[rt] scene wait" with 0 timeouts in its traced cost windows.
$eap = $ErrorActionPreference; $ErrorActionPreference = 'Stop'
$lint = @(& $flowPython @flowArgs (Join-Path $scripts 'queue_gates.py') --lint-scene-wait $here 2>&1 | ForEach-Object { "$_" }); $lintExit = $LASTEXITCODE
$ErrorActionPreference = $eap
foreach ($l in $lint) { if ($lintExit) { Write-Host "  $l" } }
Check "scene-wait gate lint (queue_gates.py --lint-scene-wait): $(if ($lintExit) { "$($lint.Count) problem(s)" } else { 'ok' })" ($lintExit -eq 0)
# And the capacity gates: RT capture drops 0 and (with rt_cascade_casters=1) cascade buffer full 0 per cost window.
$eap = $ErrorActionPreference; $ErrorActionPreference = 'Stop'
$lint = @(& $flowPython @flowArgs (Join-Path $scripts 'queue_gates.py') --lint-capacity $here 2>&1 | ForEach-Object { "$_" }); $lintExit = $LASTEXITCODE
$ErrorActionPreference = $eap
foreach ($l in $lint) { if ($lintExit) { Write-Host "  $l" } }
Check "capacity gate lint (queue_gates.py --lint-capacity): $(if ($lintExit) { "$($lint.Count) problem(s)" } else { 'ok' })" ($lintExit -eq 0)
$min = ($totalSec + $totalExpectedWaitSec + $jobs.Count * $LoadSec) / 60.0
$mb = $totalTraces * $TraceMB + $totalBursts * $BurstMB + $jobs.Count * $LogMB
Say ("estimate: plans {0} s + expected readiness {8} s (timeout allowance {7} s), {1} launches x {2} s load -> {3:N1} min; {4} PNG traces x {5} MB + logs -> {6:N0} MB" -f $totalSec, $jobs.Count, $LoadSec, $min, $totalTraces, $TraceMB, $mb, $totalWaitSec, $totalExpectedWaitSec)
Check "batch within $MaxMinutes min" ($min -le $MaxMinutes)
Check "batch within $MaxMB MB" ($mb -le $MaxMB)

function New-Root([string]$dir, [switch]$Strip) {
  New-Item -ItemType Directory -Force (Join-Path $dir 'bin'), (Join-Path $dir 'captures\queue\pending'), (Join-Path $dir "captures\live_batches\$Batch\plans") | Out-Null
  $dir = (Resolve-Path $dir).Path
  Set-Content -LiteralPath (Join-Path $dir '.queue-fake-root') 'synthetic only'
  Set-Content (Join-Path $dir 'bin\VkLayer_bg3rtx.dll') 'fake layer: scene ready at present'
  Set-Content (Join-Path $dir 'bin\bg3rtx_core.dll') 'fake core'
  Copy-Item (Join-Path $here 'plans\*.json') (Join-Path $dir "captures\live_batches\$Batch\plans")
  $t = (Get-Date).AddMinutes(-10); $entries = @()
  foreach ($f in $jobFiles) {
    $job = Get-Content $f.FullName -Raw | ConvertFrom-Json
    if ($Strip) { foreach ($k in 'expect', 'expect_every', 'expect_runner', 'forbid', 'post') { $job.PSObject.Properties.Remove($k) } }
    $dst = Join-Path $dir "captures\queue\pending\$($f.Name)"
    $job | ConvertTo-Json -Depth 16 | Set-Content $dst
    (Get-Item $dst).LastWriteTime = ($t = $t.AddSeconds(1))
    $entries += @{ hash = (Get-QueueJobHash (ConvertTo-QueueWindowJob (Get-Content $dst -Raw | ConvertFrom-Json)) $dir); tree = $dir; build = ''; runs = 1 }
  }
  $build = Get-QueueBuildIdentity $dir
  foreach ($e in $entries) { $e.build = $build }
  $auth = Join-Path $dir 'captures\queue\authorizations'; New-Item -ItemType Directory -Force $auth | Out-Null
  # TEST ONLY: fabricated receipt in a scratch root; it records no user steering and grants nothing live.
  @{ version = 1; status = 'approved'; runner_session = 'synthetic-dryrun'; user_steering = 'FAKE TEST ONLY, not user permission'
     steering_source = 'offline harness'; expires_utc = [datetimeoffset]::UtcNow.AddHours(2).ToString('o'); jobs = $entries } |
    ConvertTo-Json -Depth 12 | Set-Content (Join-Path $auth $Receipt)
  $dir
}
# The fake queue run; a line per recorded run (n of the jobs, elapsed) while it lasts.
function Invoke-Queue([string]$dir, [string]$name) {
  $env:BG3RTX_FAKE_LOAD_SEC = '3'
  $jsonl = Join-Path $dir 'captures\queue\results.jsonl'
  Start-LongrunStep $name
  $p = Start-Process powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $scripts 'test-queue.ps1')`"",
    '-DryRun', '-Root', "`"$dir`"", '-PermissionSession', 'synthetic-dryrun') -NoNewWindow -PassThru -RedirectStandardOutput "$dir.queue.out"
  $seen = 0
  while (-not $p.WaitForExit(1000)) {
    $n = @(Get-Content -LiteralPath $jsonl -ErrorAction SilentlyContinue).Count
    if ($n -ne $seen) { $seen = $n; Write-Host ("[{0:HH:mm:ss}] {1}: {2} of {3} job(s) recorded" -f (Get-Date), $name, $n, $jobs.Count) }
  }
  $recs = @(Get-Content $jsonl -ErrorAction SilentlyContinue | ConvertFrom-Json)
  Stop-LongrunStep $name "$($recs.Count) run(s) recorded"
  $recs
}

$a = New-Root (Join-Path $Out 'a-real')
$recs = @(Invoke-Queue $a 'run A')
$left = @(Get-ChildItem (Join-Path $a 'captures\queue\pending') -Filter '*.json')
if ($Independent) {
  Check "run A: all $($jobs.Count) jobs ran and each is INVALID (fake game has no expect lines)" ($recs.Count -eq $jobs.Count -and -not ($recs | Where-Object { $_.valid -or $_.outcome -notmatch '^INVALID' }))
  Check "run A: pending empty" ($left.Count -eq 0)
} else {
Check "run A: only job 1 ran and it is INVALID (fake game has no expect lines)" ($recs.Count -eq 1 -and $recs[0].job -eq $first -and -not $recs[0].valid -and $recs[0].outcome -match '^INVALID')
Check "run A: the $($jobs.Count - 1) dependent job(s) stayed in pending, not run" ($left.Count -eq $jobs.Count - 1)
}

$b = New-Root (Join-Path $Out 'b-structural') -Strip
$recs = @(Invoke-Queue $b 'run B')
Check "run B: $($jobs.Count) runs recorded, job 1 first" ($recs.Count -eq $jobs.Count -and $recs[0].job -eq $first)
Check "run B: every run valid, done, single (not session), $(if ($Unfocused) { 'unfocused' } else { 'foreground (no window_mode)' })" (-not ($recs | Where-Object { -not $_.valid -or $_.outcome -notmatch '^Run ended: done' -or $_.session -or ($Unfocused -ne [bool]$_.window_mode) }))
Check "run B: pending empty" (@(Get-ChildItem (Join-Path $b 'captures\queue\pending') -Filter '*.json').Count -eq 0)
Write-Host "scratch: $Out"
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Save-LongrunReuse "lean_dryrun-$Batch" $inputHash $said
Write-Host 'all checks passed'
