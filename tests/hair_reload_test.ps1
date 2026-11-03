$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/queue-reload.ps1')
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
Write-Output '5 reload evidence tests passed (no game)'
