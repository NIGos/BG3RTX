# Offline fake queue only. No game and no real pending inbox.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& python -m tools.live_batches.rr_hair_guide.make_job
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$plan = Get-Content (Join-Path $PSScriptRoot 'plans/ef2.json') -Raw | ConvertFrom-Json
if (@($plan | Where-Object { $_.serial -gt 0 }).Count -ne 4) { throw 'K8 count drift' }
if (@($plan | Where-Object { $_.label -like 'presented-*' -and $_.set -match '(^|,)fg_snap=16(,|$)' }).Count -ne 4) { throw 'P16 count drift' }
if (@($plan | Where-Object { $_.trace_at }).Count -ne 4) { throw 'energy trace count drift' }
foreach ($p in $plan) {
    if ($p.label -like 'timing-*' -and ($p.trace_at -or $p.serial -or $p.set -notmatch '(^|,)fg_snap=0(,|$)')) { throw 'capture contaminates timing' }
}
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    $shim = Join-Path $env:TEMP 'bg3rtx-edge-python-shim'
    New-Item -ItemType Directory -Force $shim | Out-Null
    Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
    $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch rr_hair_guide -Out $Out -MaxMinutes 5 -MaxMB 2000 -TraceMB 28 -Independent -Unfocused -MaxBursts 4 -BurstMB 165 -LoadSec 120
exit $LASTEXITCODE

