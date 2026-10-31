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
Write-Host "$count camera-input and permission checks passed"
