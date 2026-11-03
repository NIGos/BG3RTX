ï»¿# Offline fake queue only. Does not authorize or enqueue a real game job.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
$plan = @(Get-Content (Join-Path $PSScriptRoot 'plans/wf8_s8.json') -Raw | ConvertFrom-Json | ForEach-Object { $_ })
$job = Get-Content (Join-Path $PSScriptRoot 'jobs/wf8-s8-reflection-flicker.json') -Raw | ConvertFrom-Json
$state = @{}
foreach ($item in ($job.set -split ',')) { $kv = $item -split '=',2; $state[$kv[0]]=$kv[1] }
$rawBpp = 0L; $presents = 0; $rawCount = 0
foreach ($step in $plan) {
  foreach ($item in ($step.set -split ',')) { $kv = $item -split '=',2; $state[$kv[0]]=$kv[1] }
  $presents += [int]$state.fg_snap
  if ([int]$state.pan_capture -gt 0) {
    if ($state.pan_water -ne '1' -or $state.pan_water_frames -ne '8' -or $state.pan_delay_ms -ne '2000') { throw "wrong selective capture contract: $($step.label)" }
    if ($state.rt_debug -eq '25') {
      foreach ($key in 'rt_water_shadows','rt_water_ls_shadows','rt_water_refraction','rt_water_game_shadow') {
        if ($state[$key] -ne '0') { throw "reflection delta confounded by $key in $($step.label)" }
      }
    }
    if ($step.kind -eq 'rotate' -and ($step.direction -ne 1 -or $step.pixels_per_second -ne 60 -or $step.motion_start -ne 1 -or $step.motion_end -ne 5)) { throw 'turn protocol drift' }
    $rawBpp += 8 * $(if ($state.rt_debug -eq '25') {9} else {5})
    $rawCount++
  }
}
# Raw: game RGB packed 4 B + surface coverage 1 B + debug RGB packed 4 B only when debug=25.
# Presented PNG worst case: uncompressed RGB plus 1% lossless-container allowance.
# Runtime raw capture refuses >2560*1440 pixels. Display must be <=3840*2160 (half-size PNG).
$bytes = $rawBpp * 2560L * 1440L + [math]::Ceiling($presents * 1920L * 1080L * 3L * 1.01) + 70L * 1000000L
if ($rawCount -ne 5 -or $presents -ne 80 -or $bytes -ge 2000000000L) { throw "capture budget failed: raw=$rawCount presents=$presents bytes=$bytes" }
Write-Host "PASS selective capture budget: $rawCount x 8 rendered, $presents presented, worst case $bytes bytes (<2 GB); render <=2560x1440 pixels, display <=3840x2160"
# Some development environments have python.exe without the Windows py launcher.
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $shim = Join-Path $env:TEMP 'bg3rtx-wf8-python-shim'
  New-Item -ItemType Directory -Force $shim | Out-Null
  Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch water_flicker_wf8 -Out $Out -MaxMinutes 6 -MaxMB 2000 -Independent -Unfocused -MaxBursts 5
exit $LASTEXITCODE

