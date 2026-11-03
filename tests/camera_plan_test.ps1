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
foreach ($batch in 'water_flicker_wf9','water_offscreen') {
  foreach ($file in Get-ChildItem (Join-Path $repo "tools/live_batches/$batch/plans") -Filter '*.json') {
    $turns=@(Get-Content $file.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_} | Where-Object kind -eq 'rotate')
    Check ($turns.Count -gt 0) "$batch contains turn windows"
    foreach ($turn in $turns) {Check ($turn.turn_mode -eq 'keys') "$batch uses held-key turns"}
    $jobFile=Get-ChildItem (Join-Path $repo "tools/live_batches/$batch/jobs") -Filter '*.json' | Select-Object -First 1
    $job=Get-Content $jobFile.FullName -Raw | ConvertFrom-Json
    foreach ($turn in @($turns | Where-Object label -like 'cost-turn-*')) {
      $phase=@(Get-Content $file.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_}).label.IndexOf($turn.label)
      $line="[rotation-input] phase=$phase mode=keys label=$($turn.label) direction=$($turn.direction) scan=18"
      Check (@($job.expect_runner | Where-Object {$line -match $_}).Count -eq 1) "$batch key log satisfies cost window gate"
    }
  }
}
$readyStep=@{wait_log=@('temporal spec mv=1 pg=0, pipeline', 'trace spec mv=0 pg=0, pipeline')}
Check (-not (Test-CameraPlanReady $readyStep 'temporal spec mv=1 pg=0, pipeline')) 'partial pipeline readiness blocks capture'
Check (-not (Test-CameraPlanReady $readyStep "temporal spec mv=0 pg=0, pipeline`ntrace spec mv=0 pg=0, pipeline")) 'wrong specialization blocks capture'
Check (Test-CameraPlanReady $readyStep "temporal spec mv=1 pg=0, pipeline`ntrace spec mv=0 pg=0, pipeline") 'all matching specializations release capture'
Check ($runner.IndexOf('if ($step.wait_log -and -not $phaseReady)') -lt $runner.IndexOf('if ($step.trace_at -and')) 'runner checks readiness before any phase trace'
Check ($runner -match '(?s)phaseWaitAt -ge \$step.wait_log_timeout.*?pipeline readiness timed out:.*?break') 'readiness timeout aborts before trace'
Write-Host "$count camera-input and permission checks passed"
