# Offline fake queue only. No game and no real pending inbox.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& python -m tools.live_batches.rr_post_decision.make_job
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$plan = Get-Content (Join-Path $PSScriptRoot 'plans/ef3.json') -Raw | ConvertFrom-Json
if (@($plan | Where-Object { $_.serial -gt 0 }).Count -ne 8) { throw 'K8 count drift' }
if (@($plan | Where-Object { $_.label -like 'presented-*' -and $_.set -match '(^|,)fg_snap=16(,|$)' }).Count -ne 8) { throw 'P16 count drift' }
if (@($plan | Where-Object { $_.trace_at }).Count -ne 8) { throw 'energy trace count drift' }
foreach ($p in $plan) {
    if ($p.kind -ne 'static' -or $p.direction -ne 0 -or $p.pixels_per_second -ne 0) { throw 'camera input forbidden' }
    if ($p.label -like 'presented-*' -and $p.set -notmatch '(^|,)fg_snap_full=1(,|$)') { throw 'native PNG required' }
    if ($p.label -like 'timing-*' -and ($p.trace_at -or $p.serial -or $p.set -notmatch '(^|,)fg_snap=0(,|$)')) { throw 'capture contaminates timing' }
}
$states = @($plan | Where-Object { $_.label -like 'rendered-*' } | ForEach-Object { if ($_.set -match '(^|,)rr_post=(\d)(,|$)') { $Matches[2] } })
if (($states -join ',') -ne '1,0,1,0,1,1,1,1') { throw 'ABAB order drift' }
$resolve = @($plan | Where-Object { $_.label -like 'rendered-*' } | ForEach-Object { if ($_.set -match '(^|,)rr_hair_resolve=(\d)(,|$)') { $Matches[2] } })
if (($resolve -join ',') -ne '0,0,0,0,0,1,0,1') { throw 'resolve repeats drift' }
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    $shim = Join-Path $env:TEMP 'bg3rtx-edge-python-shim'
    New-Item -ItemType Directory -Force $shim | Out-Null
    Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
    $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch rr_post_decision -Out $Out -MaxMinutes 7 -MaxMB 2000 -TraceMB 28 -Independent -Unfocused -MaxBursts 8 -BurstMB 165 -LoadSec 120
exit $LASTEXITCODE

