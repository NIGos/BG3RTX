$ErrorActionPreference='Stop'
$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts\camera-plan.ps1')
. (Join-Path $repo 'scripts\queue-permission.ps1')
$count=0
function Check([bool]$condition,[string]$label) {
  if(-not $condition) { throw "FAIL $label" }
  $script:count++
}
$step=@{kind='rotate';motion_start=5;motion_end=85;pixels_per_second=60;direction=1}
$start=Get-CameraPlanSample $step 5 1920 1080
$next=Get-CameraPlanSample $step 6 1920 1080
Check ($next.x-$start.x -eq 60 -and $next.y -eq 540) '60 px/s and no vertical drift'
Check (-not (Get-CameraPlanSample $step 4.99 1920 1080).active) 'static before motion'
Check (-not (Get-CameraPlanSample $step 85 1920 1080).active) 'release at motion end'
$step.direction=-1
Check ((Get-CameraPlanSample $step 6 1920 1080).x -gt (Get-CameraPlanSample $step 7 1920 1080).x) 'reverse direction'
$step.direction=1
$last=Get-CameraPlanSample $step 12.99 1920 1080
$pause=Get-CameraPlanSample $step 13 1920 1080
$wrapped=Get-CameraPlanSample $step (13+2.0/18.0+.001) 1920 1080
Check ($last.segment -eq 0 -and -not $pause.active -and $pause.reset -and $wrapped.active -and $wrapped.segment -eq 1 -and $wrapped.x -eq $start.x) 'bounded drag wrap with a released-button frame gap'
foreach($t in @(5,5.1,13,41,84.99)) {
  $point=Get-CameraPlanSample $step $t 1920 1080
  Check ($point.x -ge 720 -and $point.x -lt 1200) "pointer remains away from edges at $t"
}
$small=Get-CameraPlanSample $step 6 65 49
Check ($small.x -gt 0 -and $small.x -lt 65 -and $small.y -gt 0 -and $small.y -lt 49) 'small window bounds'
# Actual emission timing, independent of modulo samples. No window/input APIs.
$gap=2.0/18.0
$state=@{stage='released';at=[double]::NegativeInfinity;segment=-1;x=0;y=0}
$a=@{kind='rotate';active=$true;segment=0;x=720;y=540}
Check ((Get-RotationInputAction $state $a 0).kind -eq 'reset') 'initial reset is its own event'
Check ($null -eq (Get-RotationInputAction $state $a ($gap/2))) 'press waits after actual reset'
Check ((Get-RotationInputAction $state $a ($gap+.001)).kind -eq 'press') 'settled press without same-poll drag'
$a.x=750
Check ((Get-RotationInputAction $state $a ($gap+.011)).kind -eq 'drag') 'drag follows press in later poll'
# Simulate a long scheduler stall that skips the ENTIRE scheduled reset window.
$a.segment=3;$a.x=730
Check ((Get-RotationInputAction $state $a 100).kind -eq 'release') 'skipped reset still releases alone'
Check ($null -eq (Get-RotationInputAction $state $a (100+$gap/2))) 'no reset immediately after actual release'
Check ((Get-RotationInputAction $state $a (100+$gap+.001)).kind -eq 'reset') 'reset after real release gap'
Check ($null -eq (Get-RotationInputAction $state $a (100+$gap+.002))) 'no press in reset poll or next short poll'
Check ((Get-RotationInputAction $state $a (100+2*$gap+.002)).kind -eq 'press') 'press after independent reset gap'
$inactive=@{kind='static';active=$false}
Check ((Get-RotationInputAction $state $inactive 101).kind -eq 'release') 'static phase releases once'
Check ($null -eq (Get-RotationInputAction $state $inactive 102)) 'static phase emits no pointer reset'
$a.segment=4
Check ((Get-RotationInputAction $state $a 103).kind -eq 'reset') 'resume begins with reset only'
$a.segment=5
Check ($null -eq (Get-RotationInputAction $state $a 104)) 'segment change while unpressed restarts settling'
Check ((Get-RotationInputAction $state $a (104+$gap+.001)).kind -eq 'reset') 'no stale press for skipped positioned segment'
$runner=Get-Content (Join-Path $repo 'scripts/auto-run.ps1') -Raw
Check ($runner -match 'Get-RotationInputAction \$rotationInput \$sample \$planClock.Elapsed.TotalSeconds') 'runtime consumes pure actual-time sequencer'
$errors=@();[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/auto-run.ps1'),[ref]$null,[ref]$errors)
Check ($errors.Count -eq 0) 'runner syntax remains valid'
$pulse=@{kind='pan';motion_start=1;motion_end=5;key_period_ms=200;key_down_ms=60}
Check (-not (Get-CameraPlanSample $pulse .99 1920 1080).active) 'pulse stays off before motion'
Check ((Get-CameraPlanSample $pulse 1.03 1920 1080).active) 'pulse presses during duty interval'
Check (-not (Get-CameraPlanSample $pulse 1.1 1920 1080).active) 'pulse releases between intervals'
Check ((Get-CameraPlanSample $pulse 1.23 1920 1080).active) 'pulse repeats relative to motion start'
Check (-not (Get-CameraPlanSample $pulse 5 1920 1080).active) 'pulse releases at motion end'
$pulse.key_down_ms=200
Check ((Get-CameraPlanSample $pulse 1.19 1920 1080).active) 'full duty preserves held input'
$pulse.key_down_ms=201
$rejected=$false
try { Get-CameraPlanSample $pulse 2 1920 1080 | Out-Null } catch { $rejected=$true }
Check $rejected 'pulse rejects a down interval longer than its period'
$scratch=Join-Path $env:TEMP ('bg3-permission-'+[guid]::NewGuid().ToString('N'))
$q=Join-Path $scratch 'captures\queue'
New-Item -ItemType Directory -Force $q | Out-Null
$job=@{args=@('-Foreground');foreground_authorized=$true}
Check ((Get-QueuePermissionBlock $job $scratch) -match 'Explicit live batch') 'old boolean authorization cannot replace finite receipt'
Set-Content -LiteralPath (Join-Path $q 'USER_AWAY') 'synthetic policy test only'
Check ((Get-QueuePermissionBlock $job $scratch) -match 'Explicit live batch') 'away marker cannot replace finite receipt'
Set-Content -LiteralPath (Join-Path $q 'USER_PLAYING') 'synthetic policy test only'
Check ((Get-QueuePermissionBlock $job $scratch) -match 'USER_PLAYING') 'playing takes precedence'
$dependency=@{requires_success_of=@('preflight')}
Check ([bool](Get-QueueDependencyBlock $dependency $scratch)) 'missing prerequisite blocks'
@{job='preflight';valid=$true;build_changed=$false;outcome='Run ended: game exited'} | ConvertTo-Json -Compress | Set-Content (Join-Path $q 'results.jsonl')
Check ([bool](Get-QueueDependencyBlock $dependency $scratch)) 'unexpected exit blocks rotation'
@{job='preflight';valid=$true;build_changed=$false;outcome='Run ended: done after 400s'} | ConvertTo-Json -Compress | Set-Content (Join-Path $q 'results.jsonl')
Check (-not (Get-QueueDependencyBlock $dependency $scratch)) 'completed prerequisite permits rotation'
Check ([bool](Get-QueueDependencyBlock $dependency $scratch 'another build')) 'stale prerequisite build blocks rotation'
# Temporary files are retained for inspection; this test never deletes a checkout.


$frameState=@{label='';frame=0;offset=0;segment=0}
$frameStep=@{label='wf9';frame_delta_pixels=1;direction=1}
function FrameSample($f) { Get-FrameRotationSample $frameState @{kind='rotate';active=$true;segment=0;x=0;y=540} $frameStep $f 1920 }
$x0=(FrameSample 10).x
Check ((FrameSample 10).x -eq $x0) 'no new frame means no new delta'
Check ((FrameSample 11).x -eq $x0+1) 'one pixel per observed rendered frame'
Check ((FrameSample 200).x -eq $x0+2) 'stalled input never catches up with a jump'
Check ((FrameSample 200).x -eq $x0+2) 'duplicate marker never moves twice'

$frameStep.frame_interval=4
Check ((FrameSample 203).x -eq $x0+2) 'interval waits for four frames'
Check ((FrameSample 204).x -eq $x0+3) 'interval emits one pixel after four frames'
# Exact messages through the same mapping used by auto-run (no user32 calls).
foreach ($mode in 'hold_drag','centre_hold') {
  foreach ($direction in -1,1) {
    foreach ($speed in 1,2) {
      $smooth=@{label="$mode-$direction-$speed";kind='rotate';turn_mode=$mode;motion_start=0;motion_end=3;
                pixels_per_second=1;direction=$direction;frame_delta_pixels=$speed;frame_interval=1}
      $fs=@{label='';frame=0;offset=0;segment=0}
      $rs=@{stage='released';at=[double]::NegativeInfinity;segment=-1;x=0;y=0}
      $messages=@()
      foreach ($f in 0..29) {
        if ($rs.stage -ne 'pressed') { $fs.frame=$f }
        $s=Get-FrameRotationSample $fs (Get-CameraPlanSample $smooth ($f/30.0) 1920 1080) $smooth $f 1920
        $action=Get-RotationInputAction $rs $s ($f/30.0)
        $messages+=@(Get-RotationMouseMessages $action)
        # Repeated marker/poll emits nothing, including after a stalled frame number.
        $again=Get-FrameRotationSample $fs (Get-CameraPlanSample $smooth ($f/30.0) 1920 1080) $smooth $f 1920
        Check ($again.x -eq $s.x) "$mode duplicate marker $f"
      }
      $messages+=@(Get-RotationMouseMessages (Get-RotationInputAction $rs @{kind='static';active=$false} 3))
      Check ($messages[0].message -eq 0x200 -and $messages[0].buttons -eq 0 -and $messages[0].x -eq 960) "$mode reset at centre"
      Check ($messages[1].message -eq 0x207 -and $messages[1].buttons -eq 16 -and $messages[1].x -eq 960) "$mode press at centre"
      Check ($messages[-1].message -eq 0x208 -and $messages[-1].buttons -eq 0) "$mode final release"
      Check (@($messages | Where-Object {$_.message -eq 0x207}).Count -eq 1) "$mode holds one press"
      Check (@($messages | Where-Object {$_.message -eq 0x208}).Count -eq 1) "$mode releases once"
      $drags=@($messages | Select-Object -Skip 2 | Select-Object -SkipLast 1)
      $prev=960
      foreach ($m in $drags) {
        Check ($m.message -eq 0x200 -and $m.buttons -eq 16 -and $m.y -eq 540) "$mode drag protocol"
        Check ($m.x-$prev -eq $direction*$speed) "$mode exact delta"
        $prev=$m.x
      }
      if ($mode -eq 'centre_hold') { Check ($drags.Count -eq 1) 'centre offset posted once, then held without repeated moves' }
      else { Check ($drags.Count -gt 20) 'continuous one-delta-per-frame held drag' }
      $before=$fs.offset
      $stall=Get-FrameRotationSample $fs @{active=$true} $smooth 10000 1920
      $expected=if ($mode -eq 'hold_drag') {$before+$speed} else {$speed}
      Check ($fs.offset -eq $expected) "$mode never catches up after stall"
    }
  }
}
$smooth.turn_mode='typo';$rejected=$false
try { Get-CameraPlanSample $smooth 1 1920 1080 | Out-Null } catch {$rejected=$true}
Check $rejected 'unknown mode rejected'
$smooth.turn_mode='hold_drag';$smooth.frame_interval=4;$rejected=$false
try { Get-FrameRotationSample @{} @{active=$true} $smooth 1 1920 | Out-Null } catch {$rejected=$true}
Check $rejected 'smooth mode rejects sparse frames'
$smooth.frame_interval=1;$fs=@{label=$smooth.label;frame=1;offset=959;segment=0};$rejected=$false
try { Get-FrameRotationSample $fs @{active=$true} $smooth 2 1920 | Out-Null } catch {$rejected=$true}
Check $rejected 'client edge fails closed without wrap'
$legacy=@{kind='rotate';motion_start=0;motion_end=10;pixels_per_second=60;direction=1}
foreach ($t in 0,1,7.99,8,8.12) {
  $implicit=Get-CameraPlanSample $legacy $t 1920 1080
  $legacy.turn_mode='legacy'
  $explicit=Get-CameraPlanSample $legacy $t 1920 1080
  Check (($implicit | ConvertTo-Json -Compress) -eq ($explicit | ConvertTo-Json -Compress)) 'explicit legacy matches default'
  $legacy.Remove('turn_mode')
}
$missing=@{turn_mode='hold_drag';kind='rotate';pixels_per_second=1;direction=1}
$rejected=$false
try { Get-CameraPlanSample $missing 0 1920 1080 | Out-Null } catch {$rejected=$true}
Check $rejected 'smooth mode without frame pacing rejected before runner skips sampler'
# Keys never use mouse motion, wraps, frame pacing, or duty cycling.
foreach ($direction in -1,1) {
  $keys=@{label='keys';kind='rotate';turn_mode='keys';motion_start=1;motion_end=121;direction=$direction;
          key_period_ms=200;key_down_ms=1;frame_delta_pixels=999}
  $expectedScan=if ($direction -eq -1) {0x10} else {0x12}
  Check (-not (Get-CameraPlanSample $keys .99 1920 1080).active) 'key stays up before phase motion'
  foreach ($t in 1,1.1,20,120.99) {
    $sample=Get-CameraPlanSample $keys $t 1920 1080
    Check ($sample.active -and $sample.kind -eq 'key_turn' -and $sample.scan -eq $expectedScan) 'Q/E held throughout interval including stalls'
    Check ($null -eq (Get-RotationInputAction @{stage='released'} $sample $t)) 'key turn emits no mouse rotation action'
  }
  Check (-not (Get-CameraPlanSample $keys 121 1920 1080).active) 'key releases at exclusive motion end'
}
$keys.direction=0;$rejected=$false
try {Get-CameraPlanSample $keys 1 1920 1080 | Out-Null} catch {$rejected=$true}
Check $rejected 'key mode requires a signed direction'
Check ($runner.Contains("`$MoveScan = if (`$sample.kind -eq 'key_turn') { [byte]`$sample.scan }")) 'runner uses Q/E scan from sample'
Check ($runner -match 'if \(\$sample.kind -eq ''rotate'' -and \$step.frame_delta_pixels\)') 'key mode bypasses frame mouse sampler'
foreach ($batch in @('water_flicker_wf9')) {
  $batchTurns=0
  foreach ($file in Get-ChildItem (Join-Path $repo "tools/live_batches/$batch/plans") -Filter '*.json') {
    $turns=@(Get-Content $file.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_} | Where-Object kind -eq 'rotate')
    $batchTurns += $turns.Count
    if (-not $turns.Count) { continue } # Static quality plans have no camera turns.
    foreach ($turn in $turns) {Check ($turn.turn_mode -eq 'keys') "$batch uses held-key turns"}
    $jobFile=Get-ChildItem (Join-Path $repo "tools/live_batches/$batch/jobs") -Filter '*.json' | Select-Object -First 1
    $job=Get-Content $jobFile.FullName -Raw | ConvertFrom-Json
    foreach ($turn in @($turns | Where-Object label -like 'cost-turn-*')) {
      $phase=@(Get-Content $file.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_}).label.IndexOf($turn.label)
      $line="[rotation-input] phase=$phase mode=keys label=$($turn.label) direction=$($turn.direction) scan=18"
      Check (@($job.expect_runner | Where-Object {$line -match $_}).Count -eq 1) "$batch key log satisfies cost window gate"
    }
  }
  if ($batch -eq 'water_flicker_wf9') { Check ($batchTurns -gt 0) "$batch contains turn windows" }
}
$freshStep=@{wait_log=@('ready enabled 1', 'quiet');wait_log_fresh=@('ready enabled 1','quiet')}
Check (-not (Test-CameraPlanReady $freshStep "ready enabled 1`nquiet" 'quiet')) 'cached ON cannot release a new toggle'
Check (-not (Test-CameraPlanReady $freshStep "ready enabled 1`nquiet" "ready enabled 1`nquiet")) 'fresh watch before settings acknowledgement cannot release'
$fresh="[cfg] settings read`nready enabled 1`nquiet"
Check (Test-CameraPlanReady $freshStep $fresh $fresh) 'fresh state and completion release after cfg'
$building="gi: gi/test.hlsl building in the background (the previous pipeline meanwhile)`n$fresh"
Check (-not (Test-CameraPlanReady $freshStep $building $fresh)) 'empty driver interval cannot hide queued shader build'
Check (Test-CameraPlanReady $freshStep "$building`ngi: gi/test.hlsl generation 1, pipeline 1 ms" $fresh) 'generation completion clears queued build'
$readyStep=@{wait_log=@('temporal spec mv=1 pg=0, pipeline', 'trace spec mv=0 pg=0, pipeline')}
Check (-not (Test-CameraPlanReady $readyStep 'temporal spec mv=1 pg=0, pipeline')) 'partial pipeline readiness blocks capture'
Check (-not (Test-CameraPlanReady $readyStep "temporal spec mv=0 pg=0, pipeline`ntrace spec mv=0 pg=0, pipeline")) 'wrong specialization blocks capture'
Check (Test-CameraPlanReady $readyStep "temporal spec mv=1 pg=0, pipeline`ntrace spec mv=0 pg=0, pipeline") 'all matching specializations release capture'
Check ($runner.IndexOf('if (($step.wait_log -or $step.stats_reset) -and -not $phaseReady)') -lt $runner.IndexOf('if ($null -ne $step.trace_at -and')) 'runner checks readiness before any phase trace'
Check ($runner -match '(?s)waitAnchor -ge .*?wait_log_timeout.*?pipeline readiness timed out:.*?break') 'readiness timeout aborts before trace'
Check (Test-CameraPlanReady $readyStep "temporal spec mv=1 pg=0, pipeline`ntrace spec mv=0 pg=0, pipeline`nunrelated later line") 'unchanged cached spec releases a later phase'
Check (-not (Test-CameraPlanReady @{wait_log=@('temporal.*trace')} "temporal`ntrace")) 'readiness cannot match across log lines'
Assert-CameraPlanFields @{wait_log=@('ready');wait_log_timeout=60;recenter=$true}
foreach ($bad in @(@{wait_logs=@('ready')}, @{wait_log=@('ready')},
                   @{wait_log=@('[');wait_log_timeout=1}, @{recenter='true'})) {
  $rejected=$false
  try { Assert-CameraPlanFields $bad } catch { $rejected=$true }
  Check $rejected 'unknown or invalid readiness/recenter fields fail closed'
}
# Cached/no-op transitions need cfg values, not synthetic build announcements.
$quiet='\[watch\].*\| pipelines 0 \(0 ms in driver\)'
$cached=@{set='gi_hit_sun=0';wait_log=@($quiet);wait_log_fresh=@($quiet)}
$qline='[watch] ready | pipelines 0 (0 ms in driver)'
$qack="[cfg] settings read`n[cfg] keys: gi_hit_sun=0`ngi: gi/gi_trace_rt_hit_direct.hlsl building in the background"
Check (-not (Test-CameraPlanReady $cached "$qline`n$qack" $qack)) 'cached GI needs fresh QUIET after acknowledgement'
Check (-not (Test-CameraPlanReady $cached "$qline`n$qack" "$qline`n$qack")) 'QUIET before settings read cannot release cached GI'
Check (-not (Test-CameraPlanReady $cached "$qack`n$qline" "[cfg] settings read`n$qline`n[cfg] keys: gi_hit_sun=0")) 'QUIET before keys acknowledgement cannot release cached GI'
Check (Test-CameraPlanReady $cached "$qack`n$qline" "$qack`n$qline") 'cached GI needs no generation for abandoned alternative'
Check (-not (Test-CameraPlanReady $cached "$qack`n$qline" "[cfg] settings read`n[cfg] keys: gi_hit_sun=1`n$qline")) 'wrong settings still block cached GI'
$resident=@{set='rr_post_temporal=1,gi_strength=1';wait_log=@('gi: gi/test.hlsl generation ', '\[watch\].*pipelines 0');wait_log_fresh=@('\[watch\].*pipelines 0')}
$ack="[cfg] settings read`n[cfg] keys: rr_post_temporal=1 gi_strength=1.00"
Check (Test-CameraPlanReady $resident $ack $ack) 'resident variant releases at matching cfg without watch or generation'
Check (-not (Test-CameraPlanReady $resident $ack '[cfg] settings read')) 'settings read alone does not acknowledge values'
Check (-not (Test-CameraPlanReady $resident $ack "[cfg] settings read`n[cfg] keys: rr_post_temporal=0")) 'wrong cfg value blocks resident transition'
$start="$ack`ngi: gi/test.hlsl building in the background"
Check (-not (Test-CameraPlanReady $resident $start $start)) 'post-ack build requires completion'
$crlfStart=($ack+"`nunrelated.hlsl other_pass: building in the background").Replace("`n","`r`n")
Check (-not (Test-CameraPlanReady $resident $crlfStart $crlfStart)) 'CRLF logs retain all new outstanding build records'

Check (@(Get-CameraPlanMissing $resident $start $start)[0] -eq 'gi: gi/test.hlsl generation ') 'missing reason names outstanding completion'
Check (Test-CameraPlanReady $resident "$start`ngi: gi/test.hlsl generation 2" $start) 'completion of actual build releases resident wait'
$water="$ack`nrt/water_compose.hlsl water_compose: building in the background"
Check (-not (Test-CameraPlanReady $resident $water $water)) 'outstanding water build blocks'
Check (Test-CameraPlanReady $resident "$water`n[water] rt/water_compose.hlsl water_compose: built" $water) 'water completion resolves outstanding build'
$resident.wait_log += 'explicit producer'
Check (-not (Test-CameraPlanReady $resident $ack $ack)) 'conditional pipeline witnesses preserve explicit producer wait'
# Explicit state observations cannot be replaced by settings acknowledgement.
$fresnel=@{set='rt_water_surface_fresnel=1';wait_log=@('surface Fresnel.*enabled 1', '\[watch\].*pipelines 0');wait_log_fresh=@('\[watch\].*pipelines 0')}
$fack="[cfg] settings read`n[cfg] keys: rt_water_surface_fresnel=1"
Check (-not (Test-CameraPlanReady $fresnel $fack $fack)) 'Fresnel needs observed state'
Check (-not (Test-CameraPlanReady $fresnel "surface Fresnel enabled 1`n$fack" $fack)) 'stale Fresnel state cannot release toggle'
Check (Test-CameraPlanReady $fresnel "$fack`nsurface Fresnel enabled 1" "$fack`nsurface Fresnel enabled 1") 'fresh Fresnel state releases'
$rr=@{set='rr_post_temporal=1';wait_log=@('\[rrpost\] rr_post\.hlsl:temporal pipeline built','\[rrpost\] rr_post\.hlsl:temporal_final pipeline built','\[watch\].*pipelines 0');wait_log_fresh=@('\[watch\].*pipelines 0')}
$rack="[cfg] settings read`n[cfg] keys: rr_post_temporal=1"
$completed="[rrpost] rr_post.hlsl:temporal pipeline built`n[rrpost] rr_post.hlsl:temporal_final pipeline built"
Check (-not (Test-CameraPlanReady $rr $rack $rack)) 'cold RR cannot pass cfg alone'
Check (-not (Test-CameraPlanReady $rr "$rack`n[rrpost] rr_post.hlsl:temporal pipeline built" $rack)) 'both RR completions required'
Check (Test-CameraPlanReady $rr "$completed`n$rack" $rack) 'cached RR completion evidence releases'
foreach ($event in 'building in the background','creation failed (-2)') {
 $rrlog="$completed`n$rack`n[rrpost] rr_post.hlsl:temporal pipeline $event"
 Check (-not (Test-CameraPlanReady $rr $rrlog $rack)) 'later RR build/failure invalidates cached readiness'
 Check (Test-CameraPlanReady $rr "$rrlog`n[rrpost] rr_post.hlsl:temporal pipeline built" $rack) 'new completion resolves RR build/failure'
}
Check (-not (Test-CameraPlanReady $rr "$rack`n[rrpost] rr_post.hlsl:temporal pipeline creation failed (-2)" $rack)) 'explicit RR failure without completion blocks'
# Execute the actual runtime wait block with a synthetic log and clock, no game.
# Execute the actual settings write: no burst before readiness.
$applyBegin=$runner.IndexOf('        $phaseReadyOffset=if (Test-Path')
$applyEnd=$runner.IndexOf('      # Optional selected-save reload', $applyBegin)
$applyBlock=[scriptblock]::Create($runner.Substring($applyBegin,$applyEnd-$applyBegin).TrimEnd().TrimEnd('}'))
$ini=Join-Path $scratch 'arm.ini';Set-Content $ini 'pan_capture=123'
$guardRunner=Join-Path $scratch 'arm.runner.txt';$log=Join-Path $scratch 'arm.log'
$step=@{wait_log=@('ready');serial=0;capture_at=0;set='rt=1,pan_capture=0,pan_prepare=0,pan_edge=0,pan_prepare=1,pan_edge=1,pan_capture=98601'}
$planClock=@{Elapsed=@{TotalMilliseconds=1000}};$NoActivate=$false;$panIndex=0
. $applyBlock
Check ((Get-Content $ini -Raw) -match 'pan_capture=0') 'settings write defers burst until readiness'
Check ((Get-Content $ini -Raw) -match 'pan_prepare=1' -and (Get-Content $ini -Raw) -match 'pan_edge=1') 'non-arming preparation and mode keys apply before readiness'
$begin=$runner.IndexOf('      if (($step.wait_log -or $step.stats_reset) -and -not $phaseReady)')
$end=$runner.IndexOf('      if ($step.recenter', $begin)
$waitSource=$runner.Substring($begin,$end-$begin)
# Observe the real settings write; moving it ahead of the readiness marker fails.
$waitSource=$waitSource.Replace('$iniLines | Set-Content -LiteralPath $ini', '$burstReadyAtWrite=(Get-Content $guardRunner -Raw) -match (''\[motion-ready\] phase=''+$panIndex+'' label=''+[regex]::Escape($step.label)); $iniLines | Set-Content -LiteralPath $ini')
$waitBlock=[scriptblock]::Create($waitSource)
# Replay hv6 window 6: the original burst armed at 07:01:40.963 and
# completed at 07:01:41.389, before readiness at 07:01:41.732.
# Run the actual wait/write block: no ready evidence must leave it disarmed.
$step.label='cards-b';$step.wait_log_timeout=5
$planClock=@{Elapsed=@{TotalSeconds=20;TotalMilliseconds=20000}}
$phaseReady=$false;$phaseWaitAt=10;$panCaptureSent=$true;$panIndex=6;$elapsed=0;$after=$false;$result=''
$phaseShaderProgress=@{}
Set-Content $log '07:01:40.963 [cfg] settings read'
while ($true) { . $waitBlock; $after=$true; break }
Check (-not $after -and (Get-Content $ini -Raw) -match 'pan_capture=0') 'hv6 missing readiness cannot arm explicit burst'
Set-Content $log '07:01:41.732 ready'
$phaseReady=$false;$phaseWaitAt=$null
$burstReadyAtWrite=$false
while ($true) { . $waitBlock; break }
Check $burstReadyAtWrite 'hv6 readiness marker exists at the actual burst write'
Check ((Get-Content $ini -Raw) -match 'pan_capture=98601' -and (Get-Content $ini -Raw) -match 'pan_prepare=1' -and (Get-Content $ini -Raw) -match 'pan_edge=1') 'hv6 ready arms explicit serial and preserves prepared mode with last assignment winning'
$log=Join-Path $scratch 'ready.log'
$guardRunner=Join-Path $scratch 'guard.runner.txt'
$phaseReadyOffset=0
$ini=Join-Path $scratch 'auto.ini'
Set-Content $ini 'pan_capture=0'
$phaseBurstArmed=$false
$phaseBurstSerial=98603
[IO.File]::WriteAllText($log, "ready cached in an earlier phase`nunrelated latest line")
$step=@{label='cached';wait_log=@('ready cached');wait_log_timeout=5;serial=98603}
$planClock=@{Elapsed=@{TotalSeconds=20;TotalMilliseconds=20000}}
$phaseReady=$false;$phaseWaitAt=$null;$panCaptureSent=$true;$panIndex=3;$elapsed=4;$after=$false
while ($true) { . $waitBlock; $after=$true; break }
Check ($after -and $phaseReady -and $elapsed -eq 0 -and $phaseStartMs -eq 20000) 'cached readiness resumes phase clock at zero'
Check ((Get-Content $ini -Raw) -match 'pan_capture=98603') 'actual runner arms burst after readiness'
$step.wait_log=@('missing');$phaseReady=$false;$phaseWaitAt=10;$after=$false;$result=''
while ($true) { . $waitBlock; $after=$true; break }
Check (-not $after -and $result -eq 'pipeline readiness timed out: cached; missing: missing') 'actual runtime timeout exits before downstream capture'
Check ($runner -match '(?s)if \(\$step.recenter.*?Send-PlanMouse 0x0200 0.*?\[motion-recenter\]') 'recenter posts pointer centre and emits marker'
# Cold-cache progress renews only the prefetch inactivity budget. Re-reading the
# same log, periodic summaries and failed compiles must never keep a stall alive.
$prefetch=@{wait_log=@('\[shaders\] cache: prefetched ');label='warmup';wait_log_timeout=180}
$progress=@{}
$compiled='08:53:20 [shaders] gi/test.hlsl?A=1:main compiled in 12000 ms, prefetch (generation 1)'
Check ((Get-CameraPlanWaitAnchor $progress $prefetch $compiled 100 10) -eq 10) 'historical shader completions grant no extension'
$advancing=$compiled+"`n08:55:00 [shaders] gi/next.hlsl:main compiled in 30000 ms (generation 1)"
Check ((Get-CameraPlanWaitAnchor $progress $prefetch $advancing 200 10) -eq 200) 'new completed shader renews prefetch timeout'
Check ((Get-CameraPlanWaitAnchor $progress $prefetch $advancing 300 10) -eq 200) 'same log cannot renew timeout'
$noise=$advancing+"`n[shaders] cache: 46 hits, 44 compiled, 0 failed`n[shaders] bad.hlsl:main FAILED, keeping previous in 1 ms"
Check ((Get-CameraPlanWaitAnchor $progress $prefetch $noise 380 10) -eq 200) 'summary and failure are not advancing compiles'
Check ((Get-CameraPlanWaitAnchor $progress $prefetch ($noise+"`n[shaders] cache: prefetched 90 requests") 381 10) -eq 10) 'prefetch complete restores ordinary deadline for other missing witnesses'
Check (-not (Test-CameraPlanReady $prefetch $noise)) 'compilation progress alone is never readiness'
Check (Test-CameraPlanReady $prefetch ($noise+"`n[shaders] cache: prefetched 90 requests")) 'prefetch witness releases warmup'
Check ((Get-CameraPlanWaitAnchor $progress @{wait_log=@('other')} ($noise+"`n"+$compiled) 400 10) -eq 10) 'unrelated waits keep their original timeout'
# Exercise the production wait block beyond its original deadline. Substitute
# only the sleep so one poll returns to the test without waiting or launching.
$pollBlock=[scriptblock]::Create($waitSource.Replace('Start-Sleep -Milliseconds 100', '$waited=$true;break'))
$step=$prefetch;$phaseReady=$false;$phaseWaitAt=10;$phaseReadyOffset=0;$phaseShaderProgress=@{}
[void](Get-CameraPlanWaitAnchor $phaseShaderProgress $step $compiled 10 10)
$planClock=@{Elapsed=@{TotalSeconds=200;TotalMilliseconds=200000}}
[IO.File]::WriteAllText($log,$advancing)
$after=$false;$waited=$false;$result=''
while ($true) { . $pollBlock; $after=$true;break }
Check ($waited -and -not $after -and -not $result -and -not $phaseReady) 'actual cold-cache wait continues on progress without releasing capture'
$planClock.Elapsed.TotalSeconds=380
$waited=$false;$result=''
while ($true) { . $pollBlock;break }
Check ($result -like 'pipeline readiness timed out: warmup*' -and -not $waited) 'actual stalled prefetch fails after inactivity budget'
Check ($runner.Contains('$deadline = $start.AddSeconds([Math]::Min($TimeoutSec,900))') -and
       $runner.Contains('while ((Get-Date) -lt $deadline)')) 'progress cannot extend absolute 15-minute launch cap'
# Execute production reset wait over settling, request, stale ack and exact ack.
$step=@{label='short';frames=64;settle_frames=4;stats_reset=$true;wait_log=@('ready cached');wait_log_timeout=30;wait_stats=@('gpu complete')}
$phaseReady=$false;$phaseWaitAt=$null;$phaseStatsToken=$null;$phaseSettleStart=$null;$phaseFrameStart=$null
$statsResetToken=7;$statsFrame=100;$traceWrites=0;$phaseShaderProgress=@{};$phaseBurstArmed=$true
$phaseReadyOffset=0;$panCaptureSent=$true;$planClock=@{Elapsed=@{TotalSeconds=20;TotalMilliseconds=20000}}
Set-Content $ini 'rt_stats_reset=7'
[IO.File]::WriteAllText($log,'ready cached')
$waited=$false
while ($true) { . $pollBlock;break }
Check ($waited -and $null -eq $phaseStatsToken) 'actual reset waits settling frames after settings readiness'
$statsFrame=104;$waited=$false
while ($true) { . $pollBlock;break }
Check ($waited -and $phaseStatsToken -eq 8 -and (Get-Content $ini -Raw) -match 'rt_stats_reset=8') 'actual reset writes one token edge after settling'
[IO.File]::WriteAllText($log,"ready cached`n[stats] reset token=7 frame=100")
while ($true) { . $pollBlock;break }
Check (-not $phaseReady) 'actual runtime rejects old reset acknowledgment'
[IO.File]::WriteAllText($log,"ready cached`n[stats] reset token=8 frame=105`ngpu complete")
while ($true) { . $pollBlock;break }
Check ($phaseReady -and $phaseFrameStart -eq 101 -and $phaseStatsComplete) 'actual matching reset starts one full measured period after settling'
Check ((Get-Content $guardRunner -Raw) -match 'stats_reset=8 frame=105') 'actual readiness records exact reset token and frame'
# Pure frame termination and reset protocol: timing estimates never end windows.
$short=@{frames=64;settle_frames=4;stats_reset=$true;wait_log_timeout=30;wait_stats=@('gpu complete','coverage complete')}
Assert-CameraPlanFields $short
Check $true 'reset-only phase accepts finite timeout without wait_log'
foreach ($badTimeout in @(0,-1,[double]::PositiveInfinity,[double]::NaN)) {
  $badReset=@{frames=60;stats_reset=$true;wait_log_timeout=$badTimeout}
  $rejected=$false
  try { Assert-CameraPlanFields $badReset } catch { $rejected=$true }
  Check $rejected "reset-only phase rejects invalid timeout $badTimeout"
}
$rejected=$false
try { Assert-CameraPlanFields @{frames=0;stats_reset=$true;wait_log_timeout=30} } catch { $rejected=$true }
Check $rejected 'explicit zero frame target rejected'

Check (-not (Test-CameraPlanFrameComplete $short $null 9999)) 'no acknowledged origin cannot complete frames'
Check (-not (Test-CameraPlanFrameComplete $short 100 163)) '63 frames do not satisfy 64-frame comparison'
Check (Test-CameraPlanFrameComplete $short 100 164) 'exact needed frame completes independent of elapsed seconds'
Check (-not (Test-CameraPlanStatsComplete $short 'gpu complete')) 'frame endpoint waits delayed GPU and coverage producers'
Check (Test-CameraPlanStatsComplete $short "gpu complete`ncoverage complete") 'all requested first periods complete'
Check ($null -eq (Get-CameraPlanStatsReset '[stats] reset token=6 frame=100' 7)) 'stale reset token rejected'
$ack=Get-CameraPlanStatsReset '[stats] reset token=7 frame=104' 7
Check ($ack.frame -eq 104) 'exact token supplies actual frame origin'
Check ($runner.Contains('$traceWrites-$phaseTraceWritesStart -ge $phaseTraceIndex')) 'frame endpoint awaits actual trace export'
# Execute the production endpoint condition for a seconds-only trace window.
$finishBegin=$runner.IndexOf('      if ($panCaptureSent -and $phaseFinished')
$finishEnd=$runner.IndexOf(' {', $finishBegin)
$finishBlock=[scriptblock]::Create($runner.Substring($finishBegin,$finishEnd-$finishBegin)+' { $completed=$true }')
$step=@{seconds=1;trace_at=@(0);wait_log=@()}
$panCaptureSent=$true;$phaseFinished=$true;$phaseStatsComplete=$false;$phaseReady=$false
$phaseTraceIndex=1;$phaseTraceWritesStart=5;$traceWrites=5;$completed=$false
. $finishBlock
Check (-not $completed) 'elapsed estimate and five stale exports cannot finish seconds-only trace phase'
$traceWrites=6;. $finishBlock
Check $completed 'actual export finishes seconds-only trace phase'
$step.trace_at=@(0,1);$phaseTraceIndex=1;$completed=$false;. $finishBlock
Check (-not $completed) 'all scheduled trace requests must be issued before phase ends'
$phaseTraceIndex=2;$completed=$false;. $finishBlock
Check (-not $completed) 'second trace must export as well'
$traceWrites=7;. $finishBlock
Check $completed 'all requested actual exports release phase'
Check ($runner.Contains('if (($step.frames -or $null -ne $step.trace_at) -and (Test-Path -LiteralPath $log))')) 'seconds-only trace phase consumes export log reader'
Check ($runner.Contains('if ($phaseTraceIndex -eq 0) { $phaseTraceWritesStart=$traceWrites }')) 'first request excludes already consumed warmup exports'
Write-Host "$count camera-input and permission checks passed"
