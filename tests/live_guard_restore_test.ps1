# Extract the real cleanup block; never execute a runner entry point.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/auto-run.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors | Out-String)}
$restore=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text -match '^if\(-not \$KeepRunning\)' -and $n.Extent.Text -match 'guardIniBytes'},$true)
if(-not $restore){throw 'runner settings restore block missing'}
$scratch=Join-Path $env:TEMP ('bg3-live-guard-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$ini=Join-Path $scratch 'fake-auto.ini'
$KeepRunning=$false
$fresh=$ast.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq '$guardLogFresh'},$true)
$log=Join-Path $scratch 'old-game.log';$start=Get-Date
[IO.File]::WriteAllText($log,'old forbidden line')
(Get-Item -LiteralPath $log).LastWriteTime=$start.AddMinutes(-1)
Invoke-Expression $fresh.Extent.Text
if($guardLogFresh){throw 'old game log allowed into fail-fast guard'}
(Get-Item -LiteralPath $log).LastWriteTime=$start.AddSeconds(1)
Invoke-Expression $fresh.Extent.Text
if(-not $guardLogFresh){throw 'fresh game log incorrectly blocked'}
foreach($exists in @($true,$false)){
  $guardIniBytes=if($exists){[Text.Encoding]::UTF8.GetBytes("original=1`r`n")}else{$null}
  [IO.File]::WriteAllText($ini,'changed=1')
  Invoke-Expression $restore.Extent.Text
  if($exists){
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($ini)) -ne [Convert]::ToBase64String($guardIniBytes)){throw 'original settings bytes not restored'}
  }elseif(Test-Path -LiteralPath $ini){throw 'temporary settings file not removed'}
}
# All fail-fast exits use break, so the same finally still requests an orderly
# close and restores the display/settings. No special kill/cleanup branch.
$finally=$ast.Find({param($n) $n -is [Management.Automation.Language.TryStatementAst] -and $n.Finally -and $n.Finally.Extent.Text -match 'guardIniBytes'},$true).Finally.Extent.Text
if($finally -notmatch 'CloseMainWindow' -or $finally -notmatch 'Exit-BackgroundDisplay'){throw 'normal orderly close/restore path missing'}
$status=$ast.Find({param($n) $n -is [Management.Automation.Language.HashtableAst] -and $n.Extent.Text -match 'slice_failures='},$true)
if($status.Extent.Text -notmatch 'completed=' -or $status.Extent.Text -notmatch 'failed_check=' -or $status.Extent.Text -notmatch 'failed_phase='){throw 'failure result fields missing'}
Write-Host 'live guard: existing/absent settings restored, common orderly close, result fields PASS'
# Extract only the wait helper: no runner entry point or game process.
$wait=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Wait-AftermathDump'},$true)
Invoke-Expression $wait.Extent.Text
$deviceFailure=[pscustomobject]@{device_lost=$true;slice=-1}
$dump='[aftermath] GPU crash dump written: C:\fake\crash.nv-gpudmp (42 bytes)'
function Check-Wait([string]$settings,$failure,[string]$expected,[double]$timeout=0) {
  $output=Wait-AftermathDump $settings $failure $log $timeout 6>&1 | Out-String
  if($output -notmatch $expected){throw "unexpected wait output: $output"}
}
[IO.File]::WriteAllText($log,$dump)
Check-Wait 'aftermath=1' $deviceFailure 'dump completed: .*C:\\fake\\crash.nv-gpudmp'
[IO.File]::WriteAllText($log,'[aftermath] device lost: waiting')
Check-Wait 'aftermath=1' $deviceFailure 'timed out after 0 s'
# Simulate a writer completing during a poll, without any background process.
function Start-Sleep { param($Milliseconds) [IO.File]::AppendAllText($log,"`n[aftermath] GPU crash dump written: C:\fake\late.nv-gpudmp (42 bytes)") }
Check-Wait 'preset=ultra,aftermath=1' $deviceFailure 'dump completed:' 1
Remove-Item Function:Start-Sleep
foreach($case in @(
  @{set='aftermath=0';failure=$deviceFailure},
  @{set='aftermath=1,aftermath=0';failure=$deviceFailure},
  @{set='rt=1';failure=$deviceFailure},
  @{set='aftermath=1';failure=[pscustomobject]@{device_lost=$false;slice=-1}},
  @{set='aftermath=1';failure=[pscustomobject]@{device_lost=$true;slice=0}}
)) {
  $output=Wait-AftermathDump $case.set $case.failure $log 0 6>&1 | Out-String
  if($output){throw "ineligible failure waited: $output"}
}
if($finally.IndexOf('Wait-AftermathDump') -gt $finally.IndexOf('CloseMainWindow')){throw 'wait must precede close'}
Write-Host 'Aftermath: completed, delayed writer, timeout, opt-out and non-device failures PASS'
