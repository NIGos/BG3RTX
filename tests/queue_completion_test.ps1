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
Write-Host 'session completion: 6 outcomes, legacy result and startup failures PASS'
