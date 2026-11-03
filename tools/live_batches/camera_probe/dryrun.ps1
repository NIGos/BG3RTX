# Offline fake queue only; no game launch or real authorization.
param([string]$Out='')
$ErrorActionPreference='Stop'
$plan=@(Get-Content (Join-Path $PSScriptRoot 'plans/camera_s8.json') -Raw | ConvertFrom-Json | ForEach-Object {$_})
$job=Get-Content (Join-Path $PSScriptRoot 'jobs/camera-probe-s8.json') -Raw | ConvertFrom-Json
$turns=@($plan | Where-Object kind -eq 'rotate')
if ($turns.Count -ne 5 -or ($plan | Measure-Object seconds -Sum).Sum -ne 110) { throw 'probe window contract drift' }
foreach ($mode in 'hold_drag','centre_hold') {
  $speeds=@($turns | Where-Object turn_mode -eq $mode | ForEach-Object frame_delta_pixels | Sort-Object)
  if (($speeds -join ',') -ne '1,2') { throw "missing speed for $mode" }
}
if (@($plan | Where-Object { $_.trace_at -or $_.serial -or $_.set -match 'pan_capture=[1-9]' }).Count) { throw 'unexpected raw/trace capture' }
$presents=@($plan | Where-Object { $_.set -match 'fg_snap=16' }).Count*16
$bytes=[math]::Ceiling($presents*1920L*1080L*3L*1.01)+50000000L
if ($presents -ne 80 -or $bytes -gt 600000000L -or $job.set -notmatch 'fps_cap=30,') { throw 'probe capture/speed budget drift' }
Write-Host "PASS camera probe: five turn subwindows, both candidate speeds; $presents presents, <=$bytes bytes at display <=3840x2160; no raw/trace captures"
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $shim=Join-Path $env:TEMP 'bg3rtx-camera-probe-python-shim'
  New-Item -ItemType Directory -Force $shim | Out-Null
  Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH="$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch camera_probe -Out $Out -MaxMinutes 4 -MaxMB 600 -LogMB 50 -Independent -Unfocused
exit $LASTEXITCODE
