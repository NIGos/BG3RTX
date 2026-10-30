# CPU/file checks only. Never launches, queues, authorizes or changes settings.
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path (Split-Path $PSScriptRoot))
$job=Get-Content (Join-Path $PSScriptRoot 'job.json') -Raw | ConvertFrom-Json
$path=Join-Path $PSScriptRoot 'motion.plan.json'
$plan=Get-Content $path -Raw | ConvertFrom-Json
. (Join-Path $root 'scripts/queue-permission.ps1')
if((Get-QueueFileHash $path).ToLowerInvariant() -cne $job.motion_plan_sha256){throw 'Frozen motion plan hash mismatch'}
if($job.live_batch -or -not $job.draft_only){throw 'This file is an unauthorized draft only'}
$bursts=@($plan | Where-Object serial -gt 0)
if($bursts.Count -ne 4 -or $plan.Count -ne 9 -or ($plan.seconds | Measure-Object -Sum).Sum -ne 265){throw 'Unexpected finite schedule'}
if(@($bursts | Group-Object serial | Where-Object Count -ne 1).Count){throw 'Duplicate burst serial'}
if(@($plan | Where-Object {$_.set -notmatch 'pan_inputs=0' -or $_.set -notmatch 'fg_state_every=1'}).Count){throw 'Wrong K or FG observability'}
if(@($bursts | Where-Object {$_.kind -eq 'rotate' -and ($_.capture_at -le $_.motion_start -or $_.capture_at+16/18 -ge $_.motion_end)}).Count){throw 'Burst outside motion window'}
if($job.set -notmatch 'rt_xfb_mb=1408' -or $job.set -notmatch 'fg=2' -or $job.post_gate){throw 'Wrong fixed camera/FG scope or optional evidence became release gate'}
$drive=Get-PSDrive -Name ([IO.Path]::GetPathRoot($root).TrimEnd('\').TrimEnd(':'))
if($drive.Free -lt $job.required_free_gib*1GB){throw 'Insufficient disk headroom; do not release draft'}
Write-Output "Draft valid: 4 K16 bursts, 265-second plan, 600-second job ceiling, about 8.96 GiB raw; free $([math]::Round($drive.Free/1GB,1)) GiB. Not queued; user not yet tested."
