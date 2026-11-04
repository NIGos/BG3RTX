param([string]$LogDirectory='C:/Users/quali/Documents/Code Projects/BG3RTX/captures/queue/logs')
# Offline replay of cached-build and periodic-observation readiness failures. No runner invocation.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/camera-plan.ps1')
$cases=@(
  @('hv6-strand-diagnostic','020059','warm-order'),
  @('hv5-reload-bisect','020657','warm-settle'),
  @('wsf1-s8-interface-column','021248','prefetch-interface-on'),
  @('cp4-compact-camp-join','021858','warmup-off'),
  @('pt-dialogue3','022519','warmup-camera'),
  @('wsf1-s8-interface-column','060326','partition-static-0')
)
foreach($case in $cases){
  $name,$tag,$label=$case
  $jobFile=@(Get-ChildItem (Join-Path $repo 'tools/live_batches') -Recurse -Filter "$name.json" | Where-Object {$_.Directory.Name -eq 'jobs'})[0]
  $job=Get-Content $jobFile.FullName -Raw | ConvertFrom-Json
  $planFile=Get-Item (Join-Path $repo ($job.args[$job.args.IndexOf('-MovePlan')+1] -replace '^captures[/\\]live_batches', 'tools/live_batches'))
  $limits=@(& python -c "import sys,json; from tools.live_batches.live_flow import load_job; from tools.live_batches.readiness_budget import timeouts; j,p,_=load_job(sys.argv[1]); print(json.dumps(timeouts(j,p)))" $jobFile.FullName | ConvertFrom-Json | ForEach-Object {$_})
  if ($LASTEXITCODE -ne 0) { throw 'Offline timeout calculation failed' }
  $step=@(Get-Content $planFile.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_} | Where-Object label -eq $label)[0]
  if(-not $step){throw "Missing plan: $name/$label"}
  $stem=Join-Path $LogDirectory "$name.1.$tag"
  $runner=Get-Content "$stem.runner.txt" -Raw
  if($runner -notmatch "pipeline readiness timed out: $label"){throw "Missing original failure: $name"}
  $index=@(Get-Content $planFile.FullName -Raw | ConvertFrom-Json).label.IndexOf($label)
  $window=[regex]::Match($runner, "(?m)^(\d\d:\d\d:\d\d\.\d+) \[pan-plan\] window $index .*$")
  if (-not $window.Success) { throw "Missing historical window: $name/$index" }
  # Replaying historical evidence must use its recorded settings, not a later candidate.
  $applied=[regex]::Match($window.Value, ' set=(.*?) foreground=')
  if (-not $applied.Success) { throw "Missing historical settings: $name/$index" }
  $step.set=$applied.Groups[1].Value
  $start=[timespan]::Parse($window.Groups[1].Value).TotalSeconds
  $next=[regex]::Match($runner, '(?m)^(\d\d:\d\d:\d\d\.\d+) \[pan-plan\] window '+($index+1)+' .*$')
  $end=if ($next.Success) { [timespan]::Parse($next.Groups[1].Value).TotalSeconds } else { [double]::PositiveInfinity }
  # Readiness changes only on acknowledgement, tracked build or explicit witness lines.
  # Keep the complete relevant history while avoiding quadratic scans of frame telemetry.
  $lines=@(Get-Content "$stem.log" | Where-Object {
    $line=$_
    $line -match '\[cfg\]|building in the background|generation |: built|pipeline (built|building|creation failed)' -or
      @($step.wait_log | Where-Object { $line -match $_ }).Count
  })
  # Evaluate complete log timestamp groups, as one runner poll observes them.
  $prefix=[Collections.Generic.List[string]]::new()
  $fresh=[Collections.Generic.List[string]]::new()
  $pass=$null
  for($i=0;$i -lt $lines.Count;$i++){
    $line=$lines[$i];$prefix.Add($line)
    if($line -notmatch '^(\d\d:\d\d:\d\d\.\d+)'){continue}
    $stamp=$Matches[1];$now=[timespan]::Parse($stamp).TotalSeconds
    if($now -lt $start){continue}
    if($now -ge $end){break}
    $fresh.Add($line)
    if($i+1 -lt $lines.Count -and $lines[$i+1] -match '^(\d\d:\d\d:\d\d\.\d+)' -and
       [math]::Floor([timespan]::Parse($Matches[1]).TotalSeconds*10) -eq [math]::Floor($now*10)){continue}
    if(Test-CameraPlanReady $step ($prefix -join "`n") ($fresh -join "`n")){$pass=$now;break}
  }
  if($null -ne $pass -and $pass-$start -gt $limits[$index]){throw "$name readiness exceeded timeout $($limits[$index])"}
  if($null -eq $pass){throw "$name missing: $(@(Get-CameraPlanMissing $step ($prefix -join "`n") ($fresh -join "`n")) -join '; ')"}
  Write-Host ("{0}: {1} PASS at +{2:F3} s" -f $name,$label,($pass-$start))
}



# A matching state line between settings read and keys is not fresh acknowledgement.
$step=[pscustomobject]@{set='rt_water_surface_fresnel=0';wait_log=@('surface Fresnel.*enabled 0,');wait_log_fresh=@('surface Fresnel.*enabled 0,')}
$stale="12:00:00.000 [cfg] settings read`n12:00:00.050 surface Fresnel (120 frames): enabled 0,`n12:00:00.100 [cfg] keys: rt_water_surface_fresnel=0"
if(Test-CameraPlanReady $step $stale){throw 'Pre-acknowledgement observation accepted'}
$fresh=$stale+"`n12:00:04.000 surface Fresnel (120 frames): enabled 0,"
if(-not (Test-CameraPlanReady $step $fresh)){throw 'Post-acknowledgement observation rejected'}
