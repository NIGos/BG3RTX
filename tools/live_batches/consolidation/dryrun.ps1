# Offline check of the consolidation live batch: no game, no real queue.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\consolidation\dryrun.ps1 [-Out <scratch dir>]
# 1. Static checks: every job parses, its motion plan exists and samples cleanly, timeout covers
#    the plan, expect_runner names the plan's phase count, burst serials are unique.
# 2. Run A (real jobs, fake game): job 1 cannot see its expect lines, so it is INVALID and
#    every later job must stay blocked in pending (stop-on-failure).
# 3. Run B (same jobs, expect/forbid/post stripped): all 6 run in order and are valid.
# Scratch root only (outside any checkout); the receipt written there is fake and has no authority.
param([string]$Out = (Join-Path $env:TEMP "bg3rtx-consolidation-dryrun\$(Get-Date -Format 'yyyyMMdd-HHmmss')"))
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path
$scripts = Join-Path $repo 'scripts'
. (Join-Path $scripts 'camera-plan.ps1')
. (Join-Path $scripts 'queue-permission.ps1')
. (Join-Path $scripts 'queue-window-mode.ps1')
$fail = 0
function Check([string]$what, [bool]$ok) { Write-Host ("{0} {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what); if (-not $ok) { $script:fail++ } }

$jobFiles = @(Get-ChildItem (Join-Path $here 'jobs') -Filter '*.json' | Sort-Object Name)
Check "6 job files (cb2 retired 2026-11-01)" ($jobFiles.Count -eq 6)
$serials = @()
foreach ($f in $jobFiles) {
  $job = Get-Content $f.FullName -Raw | ConvertFrom-Json
  $planRel = $job.args[[array]::IndexOf([string[]]$job.args, '-MovePlan') + 1]
  $planFile = Join-Path $here ('plans\' + (Split-Path $planRel -Leaf))
  $plan = @(Get-Content $planFile -Raw | ConvertFrom-Json | ForEach-Object { $_ })
  $sec = ($plan | Measure-Object seconds -Sum).Sum
  $ok = $true
  foreach ($step in $plan) {
    foreach ($a in ($step.set -split ',')) { if (($a -split '=', 2).Count -ne 2) { $ok = $false } }
    foreach ($t in 0, 1, ($step.seconds / 2), ($step.seconds - 0.5)) { [void](Get-CameraPlanSample $step $t 2560 1067) }
    if ($step.serial) { $serials += $step.serial; if ($step.capture_at -ge $step.seconds) { $ok = $false } }
  }
  $n = $job.name
  Check "${n}: plan $(Split-Path $planRel -Leaf) ($($plan.Count) phases, $sec s) parses and samples" $ok
  Check "${n}: timeout $($job.timeout) >= plan + 240 s load" ($job.timeout -ge $sec + 240)
  Check "${n}: expect_runner names phases=$($plan.Count)" ([bool](@($job.expect_runner) -match "phases=$($plan.Count)\\b"))
  Check "${n}: live_batch, visible_unfocused_fg, fg=2, patterns/expect present" ($job.live_batch -and $job.window_mode -eq 'visible_unfocused_fg' -and $job.set -match '(^|,)fg=2(,|$)' -and @($job.patterns).Count -and @($job.expect).Count)
  $norm = ConvertTo-QueueWindowJob $job
  Check "${n}: window mode keeps fg=2 and adds -UnfocusedFG" ($norm.set -notmatch 'fg=0' -and @($norm.args) -contains '-UnfocusedFG' -and -not $norm.session)
  Check "${n}: stop-on-failure dependency" ($n -eq 'cb1-regression-camp-fg2' -or @($job.requires_success_of) -contains 'cb1-regression-camp-fg2')
}
Check "burst serials unique ($($serials.Count))" (($serials | Select-Object -Unique).Count -eq $serials.Count)

function New-Root([string]$dir, [switch]$Strip) {
  New-Item -ItemType Directory -Force (Join-Path $dir 'bin'), (Join-Path $dir 'captures\queue\pending'), (Join-Path $dir 'captures\live_batches\consolidation\plans') | Out-Null
  $dir = (Resolve-Path $dir).Path
  Set-Content -LiteralPath (Join-Path $dir '.queue-fake-root') 'synthetic only'
  Set-Content (Join-Path $dir 'bin\VkLayer_bg3rtx.dll') 'fake layer: scene ready at present'
  Set-Content (Join-Path $dir 'bin\bg3rtx_core.dll') 'fake core'
  Copy-Item (Join-Path $here 'plans\*.json') (Join-Path $dir 'captures\live_batches\consolidation\plans')
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
    ConvertTo-Json -Depth 12 | Set-Content (Join-Path $auth 'consolidation-20261031.json')
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
Check "run A: only job 1 ran and it is INVALID (fake game has no expect lines)" ($recs.Count -eq 1 -and $recs[0].job -eq 'cb1-regression-camp-fg2' -and -not $recs[0].valid -and $recs[0].outcome -match '^INVALID')
Check "run A: the 5 dependent jobs stayed in pending, not run" ($left.Count -eq 5)

$b = New-Root (Join-Path $Out 'b-structural') -Strip
$recs = @(Invoke-Queue $b)
Check "run B: 6 runs recorded, job 1 first" ($recs.Count -eq 6 -and $recs[0].job -eq 'cb1-regression-camp-fg2')
Check "run B: every run valid, done, single (not session), visible_unfocused_fg" (-not ($recs | Where-Object { -not $_.valid -or $_.outcome -notmatch '^Run ended: done' -or $_.session -or $_.window_mode -ne 'visible_unfocused_fg' }))
Check "run B: pending empty" (@(Get-ChildItem (Join-Path $b 'captures\queue\pending') -Filter '*.json').Count -eq 0)
Write-Host "scratch: $Out"
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host 'all checks passed'
