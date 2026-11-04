param([string]$LogDirectory='C:/Users/quali/Documents/Code Projects/BG3RTX/captures/queue/logs')
# Offline replay of the five bundle13 readiness failures. No runner invocation.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/camera-plan.ps1')
$cases=@(
  @('hv6-strand-diagnostic','020059','warm-order'),
  @('hv5-reload-bisect','020657','warm-settle'),
  @('wsf1-s8-interface-column','021248','prefetch-interface-on'),
  @('cp4-compact-camp-join','021858','warmup-off'),
  @('pt-dialogue3','022519','warmup-camera')
)
foreach($case in $cases){
  $name,$tag,$label=$case
  $jobFile=@(Get-ChildItem (Join-Path $repo 'tools/live_batches') -Recurse -Filter "$name.json" | Where-Object {$_.Directory.Name -eq 'jobs'})[0]
  $job=Get-Content $jobFile.FullName -Raw | ConvertFrom-Json
  $planFile=Get-Item (Join-Path $repo ($job.args[$job.args.IndexOf('-MovePlan')+1] -replace '^captures[/\\]live_batches', 'tools/live_batches'))
  $step=@(Get-Content $planFile.FullName -Raw | ConvertFrom-Json | ForEach-Object {$_} | Where-Object label -eq $label)[0]
  if(-not $step){throw "Missing plan: $name/$label"}
  $stem=Join-Path $LogDirectory "$name.1.$tag"
  $runner=Get-Content "$stem.runner.txt" -Raw
  if($runner -notmatch "pipeline readiness timed out: $label"){throw "Missing original failure: $name"}
  $index=@(Get-Content $planFile.FullName -Raw | ConvertFrom-Json).label.IndexOf($label)
  $window=[regex]::Match($runner, "(?m)^(\d\d:\d\d:\d\d\.\d+) \[pan-plan\] window $index .*$")
  $start=[timespan]::Parse($window.Groups[1].Value).TotalSeconds
  $lines=@(Get-Content "$stem.log")
  # Evaluate complete log timestamp groups, as one runner poll observes them.
  $prefix=[Collections.Generic.List[string]]::new()
  $fresh=[Collections.Generic.List[string]]::new()
  $pass=$null
  for($i=0;$i -lt $lines.Count;$i++){
    $line=$lines[$i];$prefix.Add($line)
    if($line -notmatch '^(\d\d:\d\d:\d\d\.\d+)'){continue}
    $stamp=$Matches[1];$now=[timespan]::Parse($stamp).TotalSeconds
    if($now -lt $start){continue}
    $fresh.Add($line)
    if($i+1 -lt $lines.Count -and $lines[$i+1] -match '^(\d\d:\d\d:\d\d\.\d+)' -and
       [math]::Floor([timespan]::Parse($Matches[1]).TotalSeconds*10) -eq [math]::Floor($now*10)){continue}
    if(Test-CameraPlanReady $step ($prefix -join "`n") ($fresh -join "`n")){$pass=$now;break}
  }
  if($null -eq $pass){throw "$name missing: $(@(Get-CameraPlanMissing $step ($prefix -join "`n") ($fresh -join "`n")) -join '; ')"}
  Write-Host ("{0}: {1} PASS at +{2:F3} s" -f $name,$label,($pass-$start))
}


