$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/camera-plan.ps1')
Assert-CameraPlanFields @{label='analysis';registration_policy='per-frame-exclude-unregistered'}
$rejected=$false
try {Assert-CameraPlanFields @{trace_att=@(1)}} catch {$rejected=$true}
if(-not $rejected){throw 'unknown field accepted'}
$runner=Get-Content (Join-Path $repo 'scripts/auto-run.ps1') -Raw
$check=$runner.IndexOf('foreach ($step in $panPlan) { Assert-CameraPlanFields $step }')
$launch=$runner.IndexOf('Assert-QueueLaunchProcesses', $runner.IndexOf('camera-plan.ps1'))
if($check -lt 0 -or $check -gt $launch){throw 'field check must precede launch'}
Write-Host 'shared fields: metadata accepted, typo rejected, runner validates before launch PASS'
