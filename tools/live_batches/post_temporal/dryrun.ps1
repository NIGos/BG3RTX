# Offline fake queue only. Never stage in a watched inbox or launch BG3.
param([string]$Out='')
$ErrorActionPreference='Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Push-Location $repo
try {
  & python -m tools.live_batches.post_temporal.make_job
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  # Four fp16 AC/EB OFF bursts, four fp32 ON bursts; depth/material/final and P8.
  $r=1280L*533L; $o=1920L*800L
  $bytes=4L*(12L*$r*4L+8L*$o*4L+16L*$r*8L)+4L*(12L*$r*4L+8L*$o*4L+16L*$r*16L)+[math]::Ceiling(64L*$o*3L*1.01)+70000000L
  if ($bytes -ge 2147483648L) { throw "disk bound exceeded: $bytes" }
  Write-Host "PASS post temporal bound $bytes bytes (<2 GiB), 8 rendered K8 (36 samples) and 8 presented K8"
  if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    $shim=Join-Path $env:TEMP 'bg3rtx-post-temporal-python-shim'
    New-Item -ItemType Directory -Force $shim | Out-Null
    Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
    $env:PATH="$shim;$env:PATH"
  }
  & (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch post_temporal -Out $Out -MaxMinutes 10 -MaxMB 2048 -Independent -Unfocused -MaxBursts 4 -BurstMB 239 -TraceMB 4 -LoadSec 120
  exit $LASTEXITCODE
} finally { Pop-Location }
