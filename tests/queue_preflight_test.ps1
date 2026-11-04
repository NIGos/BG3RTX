# CPU-only function checks; never run a queue entry point or launch a game.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/test-queue.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'queue syntax errors'}
$fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-QueuePreflightBlock'},$true)
$queueScripts=Join-Path $repo 'scripts'
Invoke-Expression ($fn.Extent.Text.Replace('$PSScriptRoot', '$queueScripts'))
$script:lintCalls=0
function python {
  $script:lintCalls++
  if($args -notcontains '--preflight'){throw 'preflight flag missing'}
  $global:LASTEXITCODE=$script:lintExit
  'synthetic: expected duration 749.5 s after load; minimum 700 s; expected readiness 20 s; readiness timeout allowance 360 s'
  if($script:lintExit){'windows: impossible gate'}
}
# A py launcher, if installed elsewhere, must use this same harmless stand-in.
function py { python @args }
function Get-JobTree { 'synthetic-tree' }
$file=[pscustomobject]@{FullName='synthetic-job.json'}
$job=[pscustomobject]@{args=@('-MovePlan','synthetic-plan.json')}
$script:lintExit=1
if((Get-QueuePreflightBlock $file $job) -notmatch 'impossible gate'){throw 'lint failure did not block'}
$script:lintExit=0
if(Get-QueuePreflightBlock $file $job){throw 'passing lint blocked'}
if(Get-QueuePreflightBlock $file ([pscustomobject]@{args=@()})){throw 'non-plan job blocked'}
if($script:lintCalls -ne 2){throw 'non-plan invoked lint'}
if((Get-QueuePreflightBlock $file ([pscustomobject]@{timeout=901})) -notmatch 'exceeds 15'){throw 'non-plan duration cap missing'}
if(Get-QueuePreflightBlock $file ([pscustomobject]@{timeout=901;max_minutes=20})){throw 'explicit larger bound rejected'}
foreach($bad in @(-1,0,[double]::NaN,[double]::PositiveInfinity)){
 if((Get-QueuePreflightBlock $file ([pscustomobject]@{max_minutes=$bad})) -notmatch 'finite and positive'){throw 'invalid max_minutes accepted'}
}
if((Get-QueuePreflightBlock $file ([pscustomobject]@{session=$true;timeout=300;traces=10})) -notmatch 'exceeds 15'){throw 'session trace bounds omitted'}
$selector=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-NextJob'},$true).Extent.Text
if($selector -notmatch 'Get-QueuePreflightBlock \$next \$job'){throw 'queue selection bypasses lint'}
$run=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-SingleRun'},$true)
$status=$run.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text -match '^if \(\(Test-Path -LiteralPath \$statusFile\)'},$true)
$scratch=Join-Path $env:TEMP ('bg3-status-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
# Real result writers, scratch paths only. Refusals exit pending with one row.
foreach($name in @('Write-Record','Write-QueueRefusal')) {
 Invoke-Expression ($ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true).Extent.Text)
}
$jsonl=Join-Path $scratch 'results.jsonl';$md=Join-Path $scratch 'results.md'
$script:refusedQueueRecords=@{};$script:lastRefusal=$null
$pending=New-Item -ItemType Directory (Join-Path $scratch 'pending')
$done=New-Item -ItemType Directory (Join-Path $scratch 'done')
$file=New-Item -ItemType File (Join-Path $pending 'synthetic.json')
$job=[pscustomobject]@{name='hv5-reload-bisect';args=@('-MovePlan','synthetic-plan.json')}
$job | ConvertTo-Json | Set-Content $file.FullName
function Get-InboxDirs { $pending.FullName }
function ConvertTo-QueueWindowJob($job) { $job }
function Get-QueuePermissionBlock { $null }
function Get-QueueDependencyBlock { $null }
function Get-QueueBuildIdentity { 'synthetic-build' }
function Stamp { 'synthetic-build' }
Invoke-Expression $selector
$script:blockedQueueMessages=@{}
$script:lintExit=1
if(Get-NextJob){throw 'refused lint selected a job'}
if(Get-NextJob){throw 'refused lint selected a job on repeated poll'}
$records=@(Get-Content $jsonl | ForEach-Object {$_ | ConvertFrom-Json})
if($records.Count -ne 1 -or $records[0].valid -or $records[0].completed -or $records[0].expected_seconds -ne 769.5 -or $records[0].cap_seconds -ne 900){throw 'lint refusal record/dedup/duration failed'}
if($records[0].expected_readiness_seconds -ne 20 -or $records[0].readiness_timeout_seconds -ne 360){throw 'expected readiness and allowance conflated'}
if($records[0].outcome -notmatch '^REFUSED \(preflight\): .*impossible gate' -or (Get-Content $md -Raw) -notmatch '769.5 s; cap 900 s'){throw 'refusal outcome missing evidence'}
$block=Get-QueuePreflightBlock $file ([pscustomobject]@{timeout=901})
Write-QueueRefusal $file $job $block
if(@(Get-Content $jsonl).Count -ne 2 -or $script:lastRefusal -notmatch 'expected duration 901 s; cap 900 s'){throw 'duration refusal not recorded'}
if((Test-Path $file.FullName) -or -not(Test-Path (Join-Path $done 'synthetic.json'))){throw 'refusal did not exit pending with archived job'}
$job | ConvertTo-Json | Set-Content $file.FullName
# A player marker and unfinished prerequisite are transient, preserving the job.
$script:lintExit=1
function Get-QueuePermissionBlock { 'USER_PLAYING exists: synthetic' }
if(Get-NextJob){throw 'player-blocked job selected'}
if(-not(Test-Path $file.FullName)){throw 'player block consumed pending job'}
function Get-QueuePermissionBlock { $null }
function Get-QueueDependencyBlock { 'waiting for successful prerequisite synthetic' }
if(Get-NextJob){throw 'dependency-blocked job selected'}
if(-not(Test-Path $file.FullName)){throw 'unfinished dependency consumed pending job'}
function Get-QueueDependencyBlock { $null }
# Failed durable recording must not archive the pending file.
$originalJsonl=$jsonl;$jsonl=$scratch
function Start-Sleep {}
$writeFailed=$false
try { [void](Get-NextJob) } catch { $writeFailed=$_.Exception.Message -match 'required refusal record' }
Remove-Item Function:Start-Sleep
$jsonl=$originalJsonl
if(-not $writeFailed -or -not(Test-Path $file.FullName)){throw 'failed refusal record lost pending job'}
$script:lintExit=0
if(-not(Get-NextJob)){throw 'accepted job not selected'}
$startLine=$ast.Find({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.Extent.Text -like 'Write-Host*expected duration*cap*'},$true)
$name=$job.name
$startOutput=Invoke-Expression ($startLine.Extent.Text+' 6>&1')
if("$startOutput" -notmatch 'hv5-reload-bisect: expected duration 769.5 s; cap 900 s'){throw 'accepted start duration not printed'}
$last=$ast.EndBlock.Statements[-1]
$lastOutput=Invoke-Expression $last.Extent.Text
if($lastOutput -cne $script:lastRefusal){throw 'last stdout line hides refusal'}
$statusFile=Join-Path $scratch 'run-status.json'; $logs=$scratch; $name='synthetic'; $i=1; $t0=(Get-Date).AddSeconds(-1); $completed=$true
@{completed=$false;failed_check='producer silent: expected';failed_phase=3;slice_failures=@(@{phase=3;check='producer silent: expected';slice=0;next_phase=5})} | ConvertTo-Json -Depth 5 | Set-Content $statusFile
Invoke-Expression $status.Extent.Text
if($completed -or $runStatus.failed_phase -ne 3 -or $runStatus.slice_failures.Count -ne 1){throw 'failed runner status lost'}
$sessionCheck=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Test-SessionFailFast'},$true)
Invoke-Expression ($sessionCheck.Extent.Text.Replace('$PSScriptRoot', '$queueScripts'))
function Read-LogText { '12:00:00 forbidden producer' }
$script:sess=@{log='synthetic'}
$script:sessionEvidence=@{job=[pscustomobject]@{forbid=@('forbidden')};from=0;failure=$null}
if(-not(Test-SessionFailFast) -or $script:sessionEvidence.failure -notmatch 'forbidden log pattern'){throw 'session forbid failed'}
$script:sessionEvidence.job | Add-Member fail_fast $false
if(Test-SessionFailFast){throw 'session opt-out ignored'}
Remove-Item Function:py,Function:python
$sessionLog=Join-Path $scratch 'game.log';Set-Content $sessionLog '12:00:00 [hit material paks join] attempts 0'
$context=Join-Path $scratch 'context.json'
$producer=[pscustomobject]@{set='rt_stats=1';expect=@('\[hit material paks join\].*draws [1-9]');forbid=@();window_mode='visible_unfocused_fg'}
@{job=$producer} | ConvertTo-Json -Depth 5 | Set-Content $context
$previousContext=$env:BG3RTX_QUEUE_CONTEXT_FILE
try {
  $env:BG3RTX_QUEUE_CONTEXT_FILE=$context
  $script:sess.log=$sessionLog
  $script:sessionEvidence=@{job=$producer;from=0;failure=$null;start=(Get-Date).AddSeconds(-60);nextPoll=[datetime]::MinValue}
  if(-not(Test-SessionFailFast) -or $script:sessionEvidence.failure -notmatch '^producer silent:'){throw ('session producer silence failed: '+$script:sessionEvidence.failure)}
} finally {$env:BG3RTX_QUEUE_CONTEXT_FILE=$previousContext}
Write-Host 'queue preflight and failed status: PASS'
