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
$scratch=Join-Path $env:TEMP ('bg3-permission-'+[guid]::NewGuid().ToString('N'))
$q=Join-Path $scratch 'captures\queue'
New-Item -ItemType Directory -Force $q | Out-Null
$job=@{args=@('-Foreground');foreground_authorized=$true}
Check ((Get-QueuePermissionBlock $job $scratch) -match 'USER_AWAY') 'old authorization cannot bypass absent USER_AWAY'
Set-Content -LiteralPath (Join-Path $q 'USER_AWAY') 'synthetic policy test only'
Check (-not (Get-QueuePermissionBlock $job $scratch)) 'away allows foreground'
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
