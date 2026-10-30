# Pure CPU validation. Do not invoke test-queue, auto-run, window APIs or a game.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $repo 'scripts/camera-plan.ps1')
. (Join-Path $repo 'scripts/queue-window-mode.ps1')
$checks=0
function Check([bool]$ok,[string]$label){if(-not $ok){throw "FAIL: $label"};$script:checks++}
function Settings([string]$text){
 $result=@{}
 foreach($pair in $text -split ','){
  Check ($pair -match '^([a-zA-Z0-9_]+)=([^,]+)$') "setting syntax: $pair"
  $result[$matches[1]]=$matches[2]
 }
 return $result
}
$jobFields=@('name','tree','save','set','traces','hold','timeout','repeat','session','patterns','expect','forbid','args','expect_runner','post','post_gate','stop_on_dump','requires_success_of')
$stepFields=@('label','kind','speed','seconds','repeat_ms','scan','capture_at','serial','motion_start','motion_end','pixels_per_second','direction','set','trace_at')
$mutable=@('rt_alpha_test','rt_stats','ls_stats','rf_stats','rt_primary_check','rt_primary_epoch','rt_debug','ls_debug','gi_debug','rf_debug')
$settingSources=(Get-Content -LiteralPath (Join-Path $repo 'common/settings.h') -Raw)+(Get-Content -LiteralPath (Join-Path $repo 'bg3rtx.ini') -Raw)+(Get-Content -LiteralPath (Join-Path $repo 'scripts/auto-run.ps1') -Raw)+(Get-Content -LiteralPath (Join-Path $repo 'layer/streamline.cpp') -Raw)
$allPlans=@();$traceCount=0;$seconds=0
foreach($name in @('preflight','motion')){
 $job=Get-Content -LiteralPath (Join-Path $PSScriptRoot "$name.job.json") -Raw | ConvertFrom-Json
 $plan=Get-Content -LiteralPath (Join-Path $PSScriptRoot "$name.plan.json") -Raw | ConvertFrom-Json
 Check (-not($job.PSObject.Properties.Name | Where-Object {$_ -notin $jobFields})) "$name uses existing job fields only"
 Check (-not $job.live_batch) "$name has no authorization receipt/batch"
 Check (-not $job.session -and $job.repeat -eq 1 -and $job.traces -eq 0 -and $job.hold -ge 90) "$name bounded single run"
 Check ($job.save -ceq 'Test1') "$name named reference only"
 Check ($job.args -contains '-Foreground' -and $job.args -contains '-RealPresent' -and $job.args -contains '-Move') "$name real foreground movement path"
 Check ($job.args -notcontains '-NoActivate' -and -not $job.window_mode) "$name cannot force FG off through unfocused conversion"
 Check ([object]::ReferenceEquals($job,(ConvertTo-QueueWindowJob $job))) "$name unchanged by default window policy"
 Check ($job.args[-1] -eq "docs/live-test-drafts/m1-coverage/$name.plan.json") "$name existing MovePlan file"
 Check ($job.args[-2] -eq '-MovePlan') "$name MovePlan argument"
 Check (-not $job.post_gate -and $job.post -match 'scripts.qa.fg_log_audit --logs') "$name diagnostic report is not an M1 pass"
 $base=Settings $job.set
 foreach($key in $base.Keys){Check ($settingSources -match ('["\n]'+[regex]::Escape($key)+'["=]')) "$name known setting $key"}
 foreach($key in @('rt','rt_alpha','rt_bvh_split','rt_native_hair','rt_native_scalp','rt_sun_cascade_current','rt_cascade_casters')){
  Check ($base[$key] -eq '1') "$name startup $key enabled"
 }
 Check ($base.fg -eq '2' -and $base.headless -eq '0' -and $base.rt_omm -eq '0') "$name deciding FG2 isolates coverage"
 Check ($base.dump_raw -eq '0' -and $base.pan_capture -eq '0' -and $base.pan_prepare -eq '0') "$name bounded PNG traces, no bursts"
 foreach($pattern in @($job.patterns)+@($job.expect)+@($job.forbid)+@($job.expect_runner)){
  $null=[regex]::new($pattern);Check $true "$name valid regex"
 }
 foreach($step in $plan){
  Check (-not($step.PSObject.Properties.Name | Where-Object {$_ -notin $stepFields})) "$name existing plan fields only"
  Check ($step.kind -in @('static','rotate') -and $step.seconds -gt 0 -and $step.serial -eq 0 -and $step.capture_at -eq 0) "$name bounded supported input step"
  Check ($step.motion_start -lt $step.motion_end -and $step.motion_end -lt $step.seconds) "$name interior motion window"
  $changes=Settings $step.set
  foreach($key in $changes.Keys){Check ($key -in $mutable) "$name changes only live diagnostic/coverage settings"}
  if($step.label -match 'cost'){
   foreach($key in @('rt_stats','ls_stats','rf_stats','rt_primary_check','rt_primary_epoch')){Check ($changes[$key] -eq '0') "$name cost excludes $key"}
   Check (-not $step.trace_at) "$name cost window has no capture"
  } elseif($step.label -notmatch 'mask|visibility') {Check ($step.seconds -ge 90) "$name deciding window at least 90 seconds"}
  foreach($at in @($step.trace_at)){
   if($null -ne $at){Check ($at -gt 15 -and $at -lt $step.seconds-10) "$name bounded interior trace";$traceCount++}
  }
  $sample=Get-CameraPlanSample $step 0 3840 2160
  Check (-not $sample.active) "$name no premature input"
  if($step.kind -eq 'rotate'){
   $a=Get-CameraPlanSample $step ($step.motion_start+1) 3840 2160
   $b=Get-CameraPlanSample $step ($step.motion_start+2) 3840 2160
   Check ($a.active -and $b.active -and [Math]::Sign($b.x-$a.x) -eq $step.direction) "$name actual pure-input direction"
   Check ([Math]::Abs($b.x-$a.x) -eq $step.pixels_per_second) "$name requested pure-input speed"
  }
  $seconds+=$step.seconds
 }
 Check (($plan | Measure-Object seconds -Sum).Sum -lt $job.timeout) "$name load/cleanup deadline allowance"
 $allPlans+=@($plan)
}
$pre=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'preflight.plan.json') -Raw | ConvertFrom-Json
Check ($pre[0].set -match 'rt_alpha_test=1' -and $pre[1].set -match 'rt_alpha_test=0' -and $pre[2].set -match 'rt_alpha_test=1') 'same-run native/opaque/native control'
Check ($seconds -eq 1110 -and $traceCount -eq 5) '18.5 minute plan and five trace requests'
$motion=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'motion.job.json') -Raw | ConvertFrom-Json
Check ($motion.requires_success_of -contains 'm1-coverage-preflight') 'preflight dependency'
$rows=@($allPlans | Where-Object {$_.kind -eq 'rotate' -and $_.label -notmatch 'cost'})
Check ($rows.Count -eq 4) 'four deciding rotation windows'
foreach($speed in @(60,180)){Check (@($rows | Where-Object {$_.pixels_per_second -eq $speed} | Select-Object -ExpandProperty direction | Sort-Object -Unique).Count -eq 2) 'both directions at each speed'}
Check ('[sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x1): ok' -match ($motion.expect | Where-Object {$_ -match 'SetOptions'})) 'actual SDK success spelling'
$scalp=$motion.expect | Where-Object {$_ -match '^\\\[alpha scalp'}
Check ('[alpha scalp] native hard records 1; native camera dither records 0; invalid/missing native state 0;' -match $scalp) 'hard native scalp control accepted'
Check ('[alpha scalp] native hard records 0; native camera dither records 1; invalid/missing native state 0;' -match $scalp) 'dither native scalp control accepted'
Check (-not ('[alpha scalp] native hard records 0; native camera dither records 0; invalid/missing native state 0;' -match $scalp)) 'missing scalp coverage never accepted'
Write-Host "$checks M1 draft CPU checks passed; still BLOCKED for live release"
