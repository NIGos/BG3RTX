# Execute only the actual JSON-loading statement, never auto-run's top level.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$script=Join-Path $repo 'scripts/auto-run.ps1'
$line=Get-Content -LiteralPath $script | Where-Object {$_ -match '^\$panPlan = if \(\$MovePlan\)'} | Select-Object -First 1
if(-not $line){throw 'Missing actual MovePlan loader statement'}
$directory=Join-Path $env:TEMP ('bg3-plan-array-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
$MovePlan=Join-Path $directory 'plan.json'
Set-Content -LiteralPath $MovePlan '[{"kind":"static","seconds":90},{"kind":"rotate","seconds":90}]'
Invoke-Expression $line
if($panPlan.Count -ne 2 -or $panPlan[0] -is [array] -or $panPlan[0].kind -ne 'static' -or $panPlan[1].kind -ne 'rotate'){
 throw 'MovePlan array was nested or changed step order'
}
Set-Content -LiteralPath $MovePlan '[{"kind":"static","seconds":90}]'
Invoke-Expression $line
if(@($panPlan).Count -ne 1 -or $panPlan[0] -is [array] -or $panPlan[0].seconds -ne 90){throw 'Single-step plan is not a step'}
$MovePlan=''
Invoke-Expression $line
if(@($panPlan).Count -ne 0){throw 'Absent plan is not empty'}
Write-Host "3 actual MovePlan loader checks passed on PowerShell $($PSVersionTable.PSVersion); no runner or game invoked"
