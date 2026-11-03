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
