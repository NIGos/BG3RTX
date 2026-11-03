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
Write-Host 'session completion: 6 outcomes, legacy result and startup failures PASS'
