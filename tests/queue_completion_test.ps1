# Synthetic function tests only; never dot-source a runner entry point.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/test-queue.ps1'),[ref]$tokens,[ref]$errors)
$fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-SessionRun'},$true)
Invoke-Expression $fn.Extent.Text
function Assert-QueueActive {}
function Get-LogSize {0}
function Merge-Ini { 'base=1' }
function Set-SessionIni {$true}
function Wait-Session {$script:case -ne 'gone'}
function Wait-LogLine {$null}
function Read-LogText {'expected evidence'}
function Get-Nv153 {if($script:case -eq 'nv153'){1}else{0}}
function Get-Dumps {if($script:case -eq 'dump'){@('synthetic.dump')}}
function Get-RunEnded {'Run ended: game gone'}
$scratch=Join-Path $env:TEMP ('bg3-session-result-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $scratch 'bin') | Out-Null
Set-Content (Join-Path $scratch 'bin/bg3rtx_core.dll') 'fake'
$logs=$scratch
foreach($case in @('done','trace-timeout','gone','nv153','dump','exited')) {
 $script:case=$case
 $script:sess=@{runs=0;id='fake';generation=1;readySec=0;base='base=1';ini=(Join-Path $scratch 'auto.ini');request=(Join-Path $scratch 'request');log='unused';proc=@{HasExited=($case -eq 'exited')};coreMtime=(Get-Item (Join-Path $scratch 'bin/bg3rtx_core.dll')).LastWriteTimeUtc}
 $r=Invoke-SessionRun ([pscustomobject]@{warmup=0;patterns=@('expected')}) $case 1 $scratch $(if($case -eq 'trace-timeout'){1}else{0})
 if($r.completed -ne ($case -eq 'done')){throw "completion incorrect: $case"}
 if($r.lines -notcontains 'expected evidence'){throw 'fixture must retain matching evidence even on faults'}
}
# Evaluate the real terminal-state condition on legacy and explicit results.
$condition=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text -match "successful terminal state"},$true)
foreach($r in @(@{},@{completed=$false},@{completed=$true})) {
 $missing=@();Invoke-Expression $condition.Extent.Text
 if([bool]$missing -eq [bool]$r.completed){throw 'completion condition permits a missing/failed terminal state'}
}
$startup=$ast.FindAll({param($n) $n -is [Management.Automation.Language.HashtableAst] -and ($n.Extent.Text -match 'session = "failed"' -or $n.Extent.Text -match 'failed = \$why')},$true)
if($startup.Count -ne 2 -or @($startup | Where-Object {$_.Extent.Text -notmatch 'completed = \$false'}).Count){throw 'startup failure completion missing'}
Invoke-Expression ($ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-QueueCrashOutcome'},$true).Extent.Text)
$crashLog=Join-Path $scratch 'crash.log';$statusFile=Join-Path $scratch 'status.json'
Set-Content $crashLog '19:23:45.123 [fault] VK_ERROR_DEVICE_LOST from vkQueueSubmit'
@{last_phase=4} | ConvertTo-Json | Set-Content $statusFile
$r=@{keptLog=$crashLog;dumps=@((Join-Path $scratch 'crash/mini.dmp'));status=$statusFile;failed_phase=-1;completed=$true}
$outcome=Get-QueueCrashOutcome $r
if($outcome -notmatch '^CRASH: device lost at 19:23:45 \(phase 4\), dump ' -or -not $outcome.Contains($r.dumps[0]) -or $outcome -notmatch 'VK_ERROR_DEVICE_LOST from vkQueueSubmit'){throw 'crash detail lost'}
$r.dumps=@();$r.failed_phase=2;$r.status=$null
if((Get-QueueCrashOutcome $r) -notmatch 'phase 2'){throw 'device loss without dump hidden'}
Set-Content $crashLog '19:23:45 [crash] exception 0xc0000005'
if((Get-QueueCrashOutcome $r) -notmatch '^CRASH: game crash'){throw 'CPU crash hidden'}
Set-Content $crashLog '19:23:45 [crash] first-chance fatal minidump written: C:\synthetic\mini.dmp (error 0)'
if((Get-QueueCrashOutcome $r) -notmatch 'C:\\synthetic\\mini.dmp'){throw 'kept log minidump path lost'}
# Execute the real final result block with a crash despite a successful child
# and satisfied expectations: evidence must override INVALID and valid=true.
$r.dumps=@('synthetic.dmp');$r.completed=$true
$job=[pscustomobject]@{};$tree=$scratch;$actualBuild='fake';$before='fake'
$name='synthetic';$i=1;$t0=Get-Date;$caps=@();$lines=@();$jsonl=Join-Path $scratch 'results.jsonl';$md=Join-Path $scratch 'results.md'
function Get-QueueBuildIdentity {'fake'}
function Stamp {'fake'}
Invoke-Expression ($ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Write-Record'},$true).Extent.Text)
$run=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Run-Job'},$true)
$body=$run.Find({param($n) $n -is [Management.Automation.Language.ForStatementAst]},$true).Body.Statements
$recording=$false
foreach($statement in $body) {
 if($statement.Extent.Text -eq '$crashOutcome = Get-QueueCrashOutcome $r'){$recording=$true}
 if($recording){Invoke-Expression $statement.Extent.Text}
}
$row=Get-Content $jsonl | ConvertFrom-Json
if($row.valid -or $row.completed -or $row.outcome -notmatch '^CRASH: game crash' -or (Get-Content $md -Raw) -notmatch 'CRASH: game crash'){throw 'final result hides crash'}
Set-Content $crashLog 'ordinary evidence';$r.dumps=@('dump-only.dmp')
if((Get-QueueCrashOutcome $r) -notmatch 'dump dump-only.dmp'){throw 'dump-only crash hidden'}
$r.dumps=@()
if(Get-QueueCrashOutcome $r){throw 'ordinary run marked crash'}
# Realistic load exit: no dump/device-lost, phase -1, retained three-line tail.
$runnerFile=Join-Path $scratch 'runner.txt'
Set-Content $crashLog @('older line','loading assets','publishing hair','last frame')
Set-Content $runnerFile @('[exit] process=123 code=-36863 hex=0xFFFF7001 deadline_remaining=239 phase=-1/8','Run ended: game exited code=-36863 hex=0xFFFF7001 after 61s (exclusive)')
@{last_phase=-1;observed_exit_code=-36863;script_close_requested=$false;result='game exited code=-36863 hex=0xFFFF7001'} | ConvertTo-Json | Set-Content $statusFile
$r=@{keptLog=$crashLog;dumps=@();status=$statusFile;runner_log=$runnerFile;failed_phase=-1;completed=$false}
$outcome=Get-QueueCrashOutcome $r
if($outcome -cne 'CRASH: game exited -36863 hex=0xFFFF7001 at phase -1; last log: loading assets | publishing hair | last frame'){throw "load exit detail lost: $outcome"}
# Run the final recording block too: chains must see CRASH at the start of outcome.
$jsonl=Join-Path $scratch 'load-results.jsonl';$md=Join-Path $scratch 'load-results.md'
$recording=$false
foreach($statement in $body) {
 if($statement.Extent.Text -eq '$crashOutcome = Get-QueueCrashOutcome $r'){$recording=$true}
 if($recording){Invoke-Expression $statement.Extent.Text}
}
$row=Get-Content $jsonl | ConvertFrom-Json
if($row.valid -or $row.completed -or $row.outcome -cne $outcome){throw 'persisted load exit does not pause crash chains'}
$r.dumps=@('load-mini.dmp')
if((Get-QueueCrashOutcome $r) -notmatch '; dump load-mini.dmp'){throw 'exit dump path lost'}
$r.dumps=@()
# Status may be absent if the runner itself stopped; use its exit line/outcome.
$r.status=$null
if((Get-QueueCrashOutcome $r) -notmatch '^CRASH: game exited -36863.* at phase -1;'){throw 'runner-only exit hidden'}
$r.runner_log=$null;$r.outcome='Run ended: game exited code=-36863 hex=0xFFFF7001 after 61s (exclusive)'
if((Get-QueueCrashOutcome $r) -notmatch '^CRASH: game exited -36863'){throw 'legacy outcome exit hidden'}
$r.outcome=$null;$r.status=$statusFile
foreach($code in @(0,7,-36863)) {
 @{last_phase=-1;observed_exit_code=$code;script_close_requested=$false;result='game exited'} | ConvertTo-Json | Set-Content $statusFile
 if((Get-QueueCrashOutcome $r) -notmatch "^CRASH: game exited $code at phase -1;"){throw "unrequested exit hidden: $code"}
}
foreach($result in @('done','fail-fast: missing expected line')) {
 @{last_phase=2;observed_exit_code=$null;script_close_requested=$true;result=$result} | ConvertTo-Json | Set-Content $statusFile
 Set-Content $runnerFile @("Run ended: $result after 61s (exclusive)","[exit] script requests CloseMainWindow: result=$result phase=2/8")
 $r.runner_log=$runnerFile
 if(Get-QueueCrashOutcome $r){throw "runner close marked crash: $result"}
 $r.status=$null
 if(Get-QueueCrashOutcome $r){throw "runner-only close marked crash: $result"}
 $r.status=$statusFile
}
# Execute only the cleanup race guard, using a synthetic exited process.
@{last_phase=4;last_phase_label='passes-1-gbuffer';script_close_requested=$true} | ConvertTo-Json | Set-Content $statusFile
Set-Content $crashLog '07:30:22.465 [fault] VK_ERROR_DEVICE_LOST from the fault poll'
$r.status=$statusFile;$r.runner_log=$null
if((Get-QueueCrashOutcome $r) -notmatch 'phase 4 \[passes-1-gbuffer\]'){throw 'device-loss result lost pass-bisect window label'}
$autoTokens=$null;$autoErrors=$null
$autoAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/auto-run.ps1'),[ref]$autoTokens,[ref]$autoErrors)
if($errors.Count -or $autoErrors.Count){throw 'runner parse errors'}
$raceGuard=$autoAst.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if ($game -and $game.HasExited -and $null -eq $observedExitCode')},$true)
if(-not $raceGuard){throw 'cleanup exit guard missing'}
$game=[pscustomobject]@{HasExited=$true;ExitCode=-36863;Id=123}
$game | Add-Member ScriptMethod WaitForExit {}
$observedExitCode=$null;$scriptCloseRequested=$false;$panIndex=-1;$panPlan=@(1,2)
Invoke-Expression $raceGuard.Extent.Text
if($observedExitCode -ne -36863){throw 'cleanup race exit code lost'}
$observedExitCode=$null;$scriptCloseRequested=$true
Invoke-Expression $raceGuard.Extent.Text
if($null -ne $observedExitCode){throw 'requested close recorded as unexpected exit'}
Write-Host 'session completion and crash classification: PASS (load exit, device loss, normal/fail-fast close, cleanup race)'
