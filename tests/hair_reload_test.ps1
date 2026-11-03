$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/queue-reload.ps1')
. (Join-Path $PSScriptRoot '../scripts/camera-plan.ps1')
foreach ($value in $true,$false) { Assert-CameraPlanFields @{reload_selected=$value} }
foreach ($step in @(@{reload_selected='true'},@{reload_selected=1},@{reload_select=$true})) {
  $rejected=$false
  try { Assert-CameraPlanFields $step } catch { $rejected=$true }
  if (-not $rejected) { throw 'accepted invalid selected-save reload field' }
}
function Sample($frame,$passes,$draws) { "[auto] present ${frame}: $passes passes, $draws draws in the last frame" }
$good=@((Sample 300 5 0),(Sample 600 50 500),(Sample 900 50 500)) -join "`n"
if(-not (Test-SelectedSaveReloadEvidence $good)) { throw 'missed load then stable scene' }
$missing=@((Sample 600 50 500),(Sample 900 50 500)) -join "`n"
if(Test-SelectedSaveReloadEvidence $missing) { throw 'accepted scene without loading' }
$short=@((Sample 300 5 0),(Sample 600 50 500)) -join "`n"
if(Test-SelectedSaveReloadEvidence $short) { throw 'accepted incomplete stability interval' }
$menu=@((Sample 300 5 0),(Sample 600 40 100),(Sample 900 40 100)) -join "`n"
if(Test-SelectedSaveReloadEvidence $menu) { throw 'accepted low draw menu' }
$reset=@((Sample 300 5 0),(Sample 600 50 500),(Sample 900 4 0),(Sample 1200 50 500)) -join "`n"
if(Test-SelectedSaveReloadEvidence $reset) { throw 'did not reset after new load' }
Write-Output '5 reload evidence tests and reload field schema checks passed (no game)'
