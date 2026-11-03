# Execute only the recovery helper with synthetic Git/process state.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/merge-green.ps1'),[ref]$tokens,[ref]$errors)
$fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Undo-OwnedMerge'},$true)
Invoke-Expression $fn.Extent.Text
function git {
 $command=$args -join ' ';$script:calls+=@($command);$global:LASTEXITCODE=0
 switch($command){
  'rev-parse HEAD' {'base'}
  'rev-parse -q --verify MERGE_HEAD' {if($script:merging){$script:target}else{$global:LASTEXITCODE=1}}
  'merge --abort' {if($script:abortFails){$global:LASTEXITCODE=1}else{$script:merging=$false}}
  'reset --merge HEAD' {if($script:resetFails){$global:LASTEXITCODE=1}else{$script:merging=$false}}
  'status --porcelain --untracked-files=no' {if($script:dirty){' M tracked.txt'}}
  default {throw "unexpected Git command: $command"}
 }
}
$ownerPath=Join-Path $env:TEMP ('bg3-merge-owner-'+[guid]::NewGuid().ToString('N')+'.json')
foreach($case in @('abort','fallback','reset-fail','foreign','dirty','committed')){
 $script:calls=@();$script:merging=$case -ne 'committed';$script:target=if($case -eq 'foreign'){'other'}else{'feature'}
 $script:abortFails=$case -in @('fallback','reset-fail');$script:resetFails=$case -eq 'reset-fail';$script:dirty=$case -eq 'dirty'
 Set-Content -LiteralPath $ownerPath 'synthetic'
 $failed=$false
 try{Undo-OwnedMerge @{head='base';target='feature'}}catch{$failed=$true}
 $expectedFailure=$case -in @('reset-fail','foreign','dirty')
 if($failed -ne $expectedFailure -or (Test-Path -LiteralPath $ownerPath) -ne $expectedFailure){throw "recovery failed: $case"}
 if($case -eq 'foreign' -and $script:calls -contains 'merge --abort'){throw 'aborted foreign merge'}
 if($case -eq 'fallback' -and $script:calls -notcontains 'reset --merge HEAD'){throw 'missing reset fallback'}
}
# Recovery must run in finally, even on an exception/exit in a gate.
$try=$ast.Find({param($n) $n -is [Management.Automation.Language.TryStatementAst] -and $n.Finally.Extent.Text -match 'Undo-OwnedMerge'},$true)
if(-not $try){throw 'missing finally recovery'}
$text=$ast.Extent.Text
if($text.IndexOf('$owner | ConvertTo-Json | Set-Content') -gt $text.IndexOf('git merge --no-ff')){throw 'ownership must precede merge'}
if($text -notmatch 'ownerProcess.StartTime' -or $text -notmatch 'Undo-OwnedMerge \$previous'){throw 'hard-kill ownership recovery missing'}
Write-Host 'merge recovery: abort, fallback, refusal, verification, finally and stale owner PASS'
# Exercise the actual stale-owner branch with mocked process identity.
$stale=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text -match '\$previous = Get-Content' },$true)
function Fail($why){throw $why}
function Get-Process {param($Id,$ErrorAction) if($script:alive){[pscustomobject]@{StartTime=$script:processStart}}}
foreach($alive in @($false,$true)){
 $script:alive=$alive;$script:processStart=[datetime]'2026-11-03T12:00:00Z'
 @{pid=42;started=$script:processStart.ToUniversalTime().ToString('o');head='base';target='feature'} | ConvertTo-Json | Set-Content -LiteralPath $ownerPath
 $script:target='feature';$script:merging=$true;$script:abortFails=$false;$script:dirty=$false;$script:calls=@()
 $failed=$false
 try{Invoke-Expression $stale.Extent.Text}catch{$failed=$true}
 if($failed -ne $alive -or (Test-Path -LiteralPath $ownerPath) -ne $alive){throw 'stale owner branch failed'}
 if($alive -and $script:calls -contains 'merge --abort'){throw 'active owner was aborted'}
}
# Invoke the actual finally body after a simulated stopped gate exception.
$script:merging=$true;$script:alive=$false
Set-Content -LiteralPath $ownerPath 'synthetic'
$owner=@{head='base';target='feature'}
try {try {throw 'stopped gate'} finally {Invoke-Expression $try.Finally.Extent.Text.Trim('{','}')}} catch {if($_.Exception.Message -ne 'stopped gate'){throw}}
if($script:merging -or (Test-Path -LiteralPath $ownerPath)){throw 'finally left an owned merge'}
Write-Host 'hard-killed owner recovered, live owner refused, stopped gate finally executed PASS'
