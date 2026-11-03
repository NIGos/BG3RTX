# Offline checks only; never stage a job or launch BG3.
param()
$ErrorActionPreference='Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Push-Location $repo
try {
  . (Join-Path $repo 'scripts/low-priority.ps1')
  & python -m tools.live_batches.post_temporal.make_job --scene dialogue3
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  & python tools/live_batches/live_flow.py tools/live_batches/post_temporal/jobs/pt-dialogue3.json --preflight
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  & python tests/post_temporal_pair_test.py
  exit $LASTEXITCODE
} finally { Pop-Location }
