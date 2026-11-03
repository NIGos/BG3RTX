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
  if($script:lintExit){'windows: impossible gate'}else{'expected duration: 90 s'}
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
