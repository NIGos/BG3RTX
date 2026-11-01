# Offline check of a lean live batch (tools/live_batches/<Batch>): no game, no real queue.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\lean_dryrun.ps1 -Batch lean_regression [-Out <scratch dir>]
# 1. Static checks per job: plan parses and samples, foreground FG 2 single launch, timeout covers the
#    plan, expect_runner names the phase count and every trace, no float bursts, diagnostics
#    (rt_stats, rt_primary_check, rt_primary_identity, ls_stats, ls_check, rt_debug, rf_stats, rf_debug) on
#    exactly in "diag-*" phases, cost windows diag-off and without traces (debug-view traces only in
#    "diag-view-*"), phases 5-25 s. Batch totals against -MaxMinutes / -MaxMB.
# 2. Run A (real jobs, fake game): job 1 cannot see its expect lines, so it is INVALID and the
#    dependent jobs stay in pending (stop-on-failure). With -Independent (no requires_success_of):
#    every job runs and every one is INVALID.
# 3. Run B (expect/expect_every/forbid/post stripped): every job runs in order and is valid.
# Scratch root only (outside any checkout); the receipt written there is fake and has no authority.
param([Parameter(Mandatory)][string]$Batch, [string]$Receipt = '',
      [string]$Out = '', [double]$MaxMinutes = 20, [double]$MaxMB = 2048,
      [switch]$Independent, [double]$LoadSec = 120, [double]$TraceMB = 60, [double]$LogMB = 10)
$ErrorActionPreference = 'Stop'
$here = Join-Path $PSScriptRoot $Batch
if (-not (Test-Path $here)) { throw "no batch folder $here" }
if (-not $Out) { $Out = Join-Path $env:TEMP "bg3rtx-$Batch-dryrun\$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$scripts = Join-Path $repo 'scripts'
. (Join-Path $scripts 'camera-plan.ps1')
. (Join-Path $scripts 'queue-permission.ps1')
. (Join-Path $scripts 'queue-window-mode.ps1')
$fail = 0
function Check([string]$what, [bool]$ok) { Write-Host ("{0} {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what); if (-not $ok) { $script:fail++ } }
function Apply-Set($state, [string]$set) {
  foreach ($a in ($set -split ',' | Where-Object { $_ })) { $kv = $a -split '=', 2; if ($kv.Count -ne 2) { return $false }; $state[$kv[0]] = $kv[1] }
  $true
}
function Test-Diag($s) { foreach ($k in 'rt_stats', 'rt_primary_check', 'rt_primary_identity', 'ls_stats', 'ls_check', 'rt_debug', 'rf_stats', 'rf_debug') { if ($s[$k] -and $s[$k] -ne '0') { return $true } }; $false }

$jobFiles = @(Get-ChildItem (Join-Path $here 'jobs') -Filter '*.json' | Sort-Object Name)
$jobs = @($jobFiles | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
$first = $jobs[0].name
if (-not $Receipt) { $Receipt = "$($jobs[0].live_batch).json" }
Check "$($jobs.Count) job files, all one live_batch ($($jobs[0].live_batch))" ($jobs.Count -ge 1 -and @($jobs.live_batch | Select-Object -Unique).Count -eq 1)
$totalSec = 0; $totalTraces = 0
foreach ($job in $jobs) {
  $n = $job.name
  $planRel = $job.args[[array]::IndexOf([string[]]$job.args, '-MovePlan') + 1]
  Check "${n}: plan path is captures/live_batches/$Batch/plans/" ($planRel -like "captures/live_batches/$Batch/plans/*")
  $plan = @(Get-Content (Join-Path $here ('plans\' + (Split-Path $planRel -Leaf))) -Raw | ConvertFrom-Json | ForEach-Object { $_ })
  $sec = ($plan | Measure-Object seconds -Sum).Sum; $totalSec += $sec
  $state = @{}; $ok = Apply-Set $state $job.set
  $sampled = $true; $roles = $true; $bursts = $true; $sizes = $true; $traceLines = @(); $roleNote = ''
  for ($i = 0; $i -lt $plan.Count; $i++) {
    $step = $plan[$i]
    if (-not (Apply-Set $state $step.set)) { $ok = $false }
    try { foreach ($t in 0, 1, ($step.seconds / 2), ($step.seconds - 0.5)) { [void](Get-CameraPlanSample $step $t 2560 1067) } } catch { $sampled = $false }
    if ($step.serial -or $step.capture_at -or ($state['pan_prepare'] -and $state['pan_prepare'] -ne '0')) { $bursts = $false }
    if ($step.seconds -lt 5 -or $step.seconds -gt 25) { $sizes = $false }
    $diag = Test-Diag $state; $label = [string]$step.label
    if ($diag -ne $label.StartsWith('diag-')) { $roles = $false; $roleNote += " $label" }
    if ($label.StartsWith('cost-') -and $diag) { $roles = $false }
    if ($step.trace_at) {
      $totalTraces += @($step.trace_at).Count
      if ($label.StartsWith('cost-')) { $roles = $false; $roleNote += " $label(trace in cost window)" }
      if (@($step.trace_at | Where-Object { $_ -ge $step.seconds }).Count) { $ok = $false }
      if ($diag -and -not $label.StartsWith('diag-view-')) { $roles = $false; $roleNote += " $label(trace in diag)" }
      $traceLines += "[motion-trace] phase=$i label=$label repeat=0"
    }
  }
  Check "${n}: plan $(Split-Path $planRel -Leaf) ($($plan.Count) phases, $sec s) parses, sets are key=value, traces inside phases" $ok
  Check "${n}: every phase samples" $sampled
  Check "${n}: diagnostics on exactly in diag-* phases, never in cost windows or with a trace$roleNote" $roles
  Check "${n}: no float bursts (serial/capture_at 0, pan_prepare 0)" $bursts
  Check "${n}: phases 5-25 s" $sizes
  Check "${n}: timeout $($job.timeout) >= plan + 240 s load" ($job.timeout -ge $sec + 240)
  $er = @($job.expect_runner)
  Check "${n}: expect_runner names phases=$($plan.Count), the foreground pointer line and $($traceLines.Count) traces" (
    [bool]($er -match "phases=$($plan.Count)\\b") -and [bool]($er -match 'pointer centred in owned foreground game') -and
    -not @($traceLines | Where-Object { $t = $_; -not @($er | Where-Object { $t -match $_ }).Count }).Count)
  Check "${n}: foreground single launch, fg=2, hold >= 90, patterns/expect/forbid present" (
    -not $job.window_mode -and @($job.args) -contains '-RealPresent' -and @($job.args) -contains '-Foreground' -and
    @($job.args) -contains '-Move' -and $job.set -match '(^|,)fg=2(,|$)' -and -not $job.session -and $job.hold -ge 90 -and
    @($job.patterns).Count -and @($job.expect).Count -and @($job.forbid).Count)
  $norm = ConvertTo-QueueWindowJob $job
  Check "${n}: queue keeps the foreground args (no -NoActivate)" (@($norm.args) -notcontains '-NoActivate' -and (@($norm.args) -join ' ') -eq (@($job.args) -join ' '))
  if ($Independent) { Check "${n}: independent (no requires_success_of)" (-not $job.requires_success_of) }
  else { Check "${n}: stop-on-failure dependency" ($n -eq $first -or @($job.requires_success_of) -contains $first) }
}
$min = ($totalSec + $jobs.Count * $LoadSec) / 60.0
$mb = $totalTraces * $TraceMB + $jobs.Count * $LogMB
Write-Host ("estimate: plans {0} s, {1} launches x {2} s load -> {3:N1} min; {4} PNG traces x {5} MB + logs -> {6:N0} MB" -f $totalSec, $jobs.Count, $LoadSec, $min, $totalTraces, $TraceMB, $mb)
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
function Invoke-Queue([string]$dir) {
  $env:BG3RTX_FAKE_LOAD_SEC = '3'
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scripts 'test-queue.ps1') -DryRun -Root $dir -PermissionSession synthetic-dryrun | Out-Null
  @(Get-Content (Join-Path $dir 'captures\queue\results.jsonl') -ErrorAction SilentlyContinue | ConvertFrom-Json)
}

$a = New-Root (Join-Path $Out 'a-real')
$recs = @(Invoke-Queue $a)
$left = @(Get-ChildItem (Join-Path $a 'captures\queue\pending') -Filter '*.json')
if ($Independent) {
  Check "run A: all $($jobs.Count) jobs ran and each is INVALID (fake game has no expect lines)" ($recs.Count -eq $jobs.Count -and -not ($recs | Where-Object { $_.valid -or $_.outcome -notmatch '^INVALID' }))
  Check "run A: pending empty" ($left.Count -eq 0)
} else {
Check "run A: only job 1 ran and it is INVALID (fake game has no expect lines)" ($recs.Count -eq 1 -and $recs[0].job -eq $first -and -not $recs[0].valid -and $recs[0].outcome -match '^INVALID')
Check "run A: the $($jobs.Count - 1) dependent job(s) stayed in pending, not run" ($left.Count -eq $jobs.Count - 1)
}

$b = New-Root (Join-Path $Out 'b-structural') -Strip
$recs = @(Invoke-Queue $b)
Check "run B: $($jobs.Count) runs recorded, job 1 first" ($recs.Count -eq $jobs.Count -and $recs[0].job -eq $first)
Check "run B: every run valid, done, single (not session), foreground (no window_mode)" (-not ($recs | Where-Object { -not $_.valid -or $_.outcome -notmatch '^Run ended: done' -or $_.session -or $_.window_mode }))
Check "run B: pending empty" (@(Get-ChildItem (Join-Path $b 'captures\queue\pending') -Filter '*.json').Count -eq 0)
Write-Host "scratch: $Out"
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host 'all checks passed'
