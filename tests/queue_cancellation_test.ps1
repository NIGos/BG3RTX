# Offline only. Every process is a scratch fake; no game or real queue is used.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
$checks=0
function Check($ok,$label){if(-not $ok){throw "FAIL: $label"};$script:checks++}
$base=Join-Path $env:TEMP ('bg3-queue-cancel-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $base | Out-Null
function Wait-File($path,$proc){
 $deadline=(Get-Date).AddSeconds(15)
 while(-not(Test-Path -LiteralPath $path)){
  if($proc.HasExited -or (Get-Date)-ge $deadline){throw "fake did not start: $path"};Start-Sleep -Milliseconds 50
 }
}
foreach($case in @('single-revoked','command-revoked','single-playing','single-deadline','single-expired','single-foreign')){
 $tree=Join-Path $base $case
 New-Item -ItemType Directory -Force (Join-Path $tree 'bin'),(Join-Path $tree 'scripts'),(Join-Path $tree 'captures/queue/pending'),(Join-Path $tree 'captures/queue/authorizations') | Out-Null
 Set-Content (Join-Path $tree '.queue-fake-root') 'offline only'
 Set-Content (Join-Path $tree 'bin/VkLayer_bg3rtx.dll') 'fake layer'
 Set-Content (Join-Path $tree 'bin/bg3rtx_core.dll') 'fake core'
 # Owned phase stand-in remains active until finally stops it.
 @'
import os,time
from pathlib import Path
root=Path(__file__).resolve().parent.parent
(root/'phase.pid').write_text(str(os.getpid()))
while True: time.sleep(.1)
'@ | Set-Content (Join-Path $tree 'scripts/live_phases.py')
 Set-Content (Join-Path $tree 'phases.json') '{}'
 $job=[pscustomobject]@{name=$case;live_batch='batch';traces=0;timeout=10;phases='phases.json';patterns=@('fake');expect=@('fake')}
 if($case -eq 'single-deadline'){$job.timeout=1}
 if($case -eq 'command-revoked'){
 @'
$ErrorActionPreference='Stop'
Set-Content (Join-Path $PSScriptRoot 'captures/auto.ini') 'fake=1'
Set-Content (Join-Path $PSScriptRoot 'command.started') 'owned fake'
while(-not(Test-Path -LiteralPath $env:BG3RTX_QUEUE_QUIT_FILE)){Start-Sleep -Milliseconds 50}
Set-Content (Join-Path $PSScriptRoot 'command.stopped') 'cooperative quit'
Write-Output 'Run ended: queue cancelled'
'@ | Set-Content (Join-Path $tree 'command.ps1')
 $job | Add-Member command ('powershell -NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $tree 'command.ps1')+'"')
 }
 $job | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $tree "captures/queue/pending/$case.json")
 $grant=@{version=1;status='approved';runner_session='cancel-test';user_steering='SYNTHETIC TEST ONLY';steering_source='offline test';expires_utc=[datetimeoffset]::UtcNow.AddMinutes(5).ToString('o');jobs=@(@{hash=(Get-QueueJobHash $job $tree);tree=$tree;build=(Get-QueueBuildIdentity $tree);runs=1})}
 $receipt=Join-Path $tree 'captures/queue/authorizations/batch.json'
 $grant | ConvertTo-Json -Depth 10 | Set-Content $receipt
 $env:BG3RTX_FAKE_LOAD_SEC='30'
 $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $repo 'scripts/test-queue.ps1')+'" -DryRun -Root "'+$tree+'" -PermissionSession cancel-test'
 $proc=Start-Process powershell -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $tree 'runner.txt') -RedirectStandardError (Join-Path $tree 'runner.err')
 try {
  Wait-File (Join-Path $tree 'phase.pid') $proc
  $phasePid=[int](Get-Content (Join-Path $tree 'phase.pid'))
  $phase=Get-Process -Id $phasePid -ErrorAction Stop;[void]$phase.Handle
  if($case -eq 'command-revoked'){Wait-File (Join-Path $tree 'command.started') $proc}
  else{Wait-File (Join-Path $tree 'captures/auto.ini') $proc}
  if($case -match 'revoked'){$grant.status='revoked';$grant | ConvertTo-Json -Depth 10 | Set-Content $receipt}
  if($case -eq 'single-playing'){Set-Content (Join-Path $tree 'captures/queue/USER_PLAYING') 'synthetic'}
  if($case -eq 'single-expired'){$grant.expires_utc=[datetimeoffset]::UtcNow.AddSeconds(-1).ToString('o');$grant | ConvertTo-Json -Depth 10 | Set-Content $receipt}
  if($case -eq 'single-foreign'){Set-Content (Join-Path $tree 'fake_unowned_game') 'synthetic'}
  Check ($proc.WaitForExit(15000)) "$case stops promptly"
  $row=Get-Content (Join-Path $tree 'captures/queue/results.jsonl') | ConvertFrom-Json
  Check (-not $row.valid -and $row.outcome -match 'cancelled') "$case never passes"
  Check ($phase.WaitForExit(1000)) "$case phase finally cleanup"
  if($case -eq 'command-revoked'){Check (Test-Path (Join-Path $tree 'command.stopped')) 'command received inherited cooperative quit'}
  else{Check ((Get-Content (Join-Path $tree 'bg3rtx.log') -Raw) -match 'game closed') 'single fake cleanly closed'}
 } finally {if(-not $proc.HasExited){$proc.Kill();$proc.WaitForExit()}}
}
# Execute the real function body with owned process mocks: the second launch
# throws after the phase starts. No real process is launched by this case.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/test-queue.ps1'),[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'runner syntax valid'
$fn=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-SingleRun'},$true)
& {
 param($body,$tree)
 $q=$tree;$logs=$tree;$root=$tree;$DryRun=$true;$script:phaseKilled=$false;$script:starts=0
 function Assert-QueueRuntime {}
 function Assert-QueueActive {}
 function Join-Args($a){$a -join ' '}
 function Start-Process {
  $script:starts++
  if($script:starts -gt 1){throw 'synthetic child start failure'}
  $fake=[pscustomobject]@{HasExited=$false}
  $fake | Add-Member ScriptMethod Kill {$script:phaseKilled=$true;$this.HasExited=$true}
  $fake | Add-Member ScriptMethod WaitForExit {}
  return $fake
 }
 $queueScripts=Join-Path $repo "scripts"
 Invoke-Expression ($body.Replace('$PSScriptRoot','$queueScripts'))
 $env:BG3RTX_QUEUE_QUIT_FILE='preserve-parent-value'
 try {Invoke-SingleRun ([pscustomobject]@{phases='phases.json'}) 'throw' 1 $tree 0 5;throw 'expected launch failure'}
 catch {Check ($_.Exception.Message -eq 'synthetic child start failure') ("original child-launch exception preserved: "+$_.Exception.Message)}
 Check $script:phaseKilled 'throw path stops already launched phase'
 Check ($env:BG3RTX_QUEUE_QUIT_FILE -eq 'preserve-parent-value') 'throw path restores inherited quit environment'
 Remove-Item Env:BG3RTX_QUEUE_QUIT_FILE
} $fn.Extent.Text $base
Write-Host "$checks cancellation checks passed; scratch $base"
