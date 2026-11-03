# Offline fake queue only. Never stage anything in a real pending inbox.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'make_job.py')
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$plan = Get-Content (Join-Path $PSScriptRoot 'plans/ef1.json') -Raw | ConvertFrom-Json
$raw = @($plan | Where-Object { $_.serial -gt 0 })
$shown = @($plan | Where-Object { $_.label -like 'presented-*' })
if ($raw.Count -ne 9 -or $shown.Count -ne 9) { throw 'capture count drift' }
foreach ($step in $raw) {
    if ($step.set -notmatch "(?:^|,)pan_capture=$($step.serial)(?:,|$)") { throw 'phase set overrides the burst serial' }
}
foreach ($step in $shown) {
    if ($step.set -notmatch '(?:^|,)fg_snap=16(?:,|$)') { throw 'present count drift' }
}
# Worst case at the runtime pan_edge extent limits, including PNG container slack.
# Raw K8: depth x8, material/albedo x2 each, final x8, all nearest half size.
$renderPixels = 1280L * 533L
$outputPixels = 1920L * 800L
$bytes = 9L * (12L * $renderPixels * 4L + 8L * $outputPixels * 4L +
    [math]::Ceiling(16L * $outputPixels * 3L * 1.01)) + 70000000L
if ($bytes -ge 2000000000L) { throw "disk budget exceeded: $bytes" }
Write-Host "PASS edge capture bound $bytes bytes (<2 GB), 9 K8 + 9 P16; output <=3840x1600, render <=2560x1067"
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    $shim = Join-Path $env:TEMP 'bg3rtx-edge-python-shim'
    New-Item -ItemType Directory -Force $shim | Out-Null
    Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
    $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch edge_flicker -Out $Out -MaxMinutes 5 -MaxMB 2000 -Independent -Unfocused -MaxBursts 9 -BurstMB 165 -LoadSec 120
exit $LASTEXITCODE
