# Receipts RECORD actual explicit user steering; writing one never creates authority.
# No function here issues a grant. Coordinator prepares receipts only after consent.
# Schema1: status, user_steering, steering_source, expires_utc, runner_session,
# jobs[{hash,tree,build,runs}]. Extend an allowlist only within actual approved
# iterative steering; every released candidate/job is frozen independently.
function Get-QueueTextHash([string]$text) {
  $sha=[Security.Cryptography.SHA256]::Create()
  try { ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Get-QueueJobHash($job,[string]$tree) {
  $text=$job | ConvertTo-Json -Depth 64 -Compress
  if ($job.phases) {
    $phase=Join-Path $tree $job.phases
    if (-not (Test-Path -LiteralPath $phase -PathType Leaf)) { throw 'Missing frozen phase input' }
    $text+='|phase:'+ (Get-FileHash -LiteralPath $phase -Algorithm SHA256).Hash
  }
  Get-QueueTextHash $text
}
function Get-QueueBuildIdentity([string]$tree) {
  $tree=[IO.Path]::GetFullPath($tree)
  $files=@('bin/VkLayer_bg3rtx.dll','bin/bg3rtx_core.dll')
  foreach($file in $files) { if(-not(Test-Path -LiteralPath (Join-Path $tree $file) -PathType Leaf)){throw "Missing frozen binary $file"} }
  foreach($dir in @('shaders','common','scripts')) {
    $path=Join-Path $tree $dir
    if(Test-Path -LiteralPath $path){$files+=@(Get-ChildItem -LiteralPath $path -Recurse -File | Where-Object {$_.Extension -in @('.h','.hlsl','.hlsli','.ps1','.py','.txt')} | ForEach-Object {$_.FullName.Substring($tree.Length+1)})}
  }
  $revision='synthetic-no-git'
  if(Test-Path -LiteralPath (Join-Path $tree '.git')){
    $revision=((& git -C $tree rev-parse HEAD) -join "`n")
    if($LASTEXITCODE -ne 0){throw 'Cannot identify candidate revision'}
    $revision+="`n"+((& git -C $tree diff HEAD --binary) -join "`n")
    if($LASTEXITCODE -ne 0){throw 'Cannot freeze candidate source changes'}
  }
  $rows=@($files | Sort-Object -Unique | ForEach-Object {$_+':'+(Get-FileHash -LiteralPath (Join-Path $tree $_) -Algorithm SHA256).Hash})
  Get-QueueTextHash ($revision+"`n"+($rows -join "`n"))
}
function Get-QueuePermissionBlock($job,[string]$queueRoot,[string]$tree='', [string]$build='',
    [string]$session='', [string]$runner='', [switch]$InFlight) {
  if(Test-Path -LiteralPath (Join-Path $queueRoot 'captures/queue/USER_PLAYING')){return 'USER_PLAYING exists'}
  if([string]$job.live_batch -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$'){return 'Explicit live batch authorization required'}
  if(-not $tree -or -not $build -or -not $session -or -not $runner){return 'Missing authorized tree/build/runner session'}
  $path=Join-Path $queueRoot "captures/queue/authorizations/$($job.live_batch).json"
  try {
    $grant=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if($grant.version -ne 1 -or $grant.status -ne 'approved' -or -not ([string]$grant.user_steering).Trim() -or -not ([string]$grant.steering_source).Trim()){return 'Missing explicit user steering or revoked batch'}
    if([datetimeoffset]$grant.expires_utc -le [datetimeoffset]::UtcNow){return 'Expired live batch'}
    $timeout=if($job.timeout){[int]$job.timeout}else{300}
    if($timeout -lt 1 -or $timeout -gt 86400 -or (-not $InFlight -and [datetimeoffset]::UtcNow.AddSeconds($timeout) -gt [datetimeoffset]$grant.expires_utc)){return 'Run timeout outside approved window'}
    if($grant.runner_session -cne $session){return 'Runner session outside authorized batch'}
    $hash=Get-QueueJobHash $job $tree
    $entry=@($grant.jobs | Where-Object {$_.hash -ceq $hash -and [IO.Path]::GetFullPath($_.tree) -ieq [IO.Path]::GetFullPath($tree) -and $_.build -ceq $build})
    if($entry.Count -ne 1 -or [string]$entry[0].runs -notmatch '^([1-9]|[1-9][0-9]|100)$'){return 'Job/tree/build outside approved finite scope'}
    $repeat=if($job.repeat){[int]$job.repeat}else{1}
    if($repeat -lt 1 -or $repeat -gt [int]$entry[0].runs){return 'Repeat count outside approved scope'}
    $statePath=$path+'.state.json'
    if(Test-Path -LiteralPath $statePath){
      $state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
      if($state.version -ne 1 -or $null -eq $state.closed -or $state.closed -or $state.runner -cne $runner){return 'Consumed batch or runner restart rejected'}
      foreach($row in @($state.used)){if($row.hash -notmatch '^[a-f0-9]{64}$' -or [string]$row.runs -notmatch '^([0-9]|[1-9][0-9]|100)$'){return 'Malformed consumed-run state'}}
      if(@($state.used | Group-Object hash | Where-Object Count -gt 1).Count){return 'Duplicate consumed-run state'}
      $used=@($state.used | Where-Object {$_.hash -ceq $hash} | Select-Object -First 1)
      if(-not $InFlight -and $used.Count -and [int]$used[0].runs -ge [int]$entry[0].runs){return 'Consumed job allowance'}
    }
    return ''
  } catch {return 'Missing or invalid live batch receipt'}
}
function Use-QueuePermission($job,[string]$queueRoot,[string]$tree,[string]$build,[string]$session,[string]$runner) {
  $path=Join-Path $queueRoot "captures/queue/authorizations/$($job.live_batch).json"
  $lock=[IO.File]::Open($path+'.lock','OpenOrCreate','ReadWrite','None')
  try {
    $block=Get-QueuePermissionBlock $job $queueRoot $tree $build $session $runner
    if($block){throw $block}
    $statePath=$path+'.state.json'
    $state=if(Test-Path -LiteralPath $statePath){Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json}else{[pscustomobject]@{version=1;runner=$runner;closed=$false;used=@()}}
    $hash=Get-QueueJobHash $job $tree
    $used=@($state.used | Where-Object {$_.hash -ceq $hash} | Select-Object -First 1)
    if($used.Count){$used[0].runs=[int]$used[0].runs+1}else{$state.used+=@([pscustomobject]@{hash=$hash;runs=1})}
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statePath
    return $statePath
  } finally {$lock.Dispose()}
}
function Close-QueuePermission([string]$statePath,[string]$runner) {
  if(Test-Path -LiteralPath $statePath){$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json;if($state.runner -ceq $runner){$state.closed=$true;$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statePath}}
}

function Get-QueueDependencyBlock($job, [string]$queueRoot, [string]$currentBuild = '') {
  if (-not $job.requires_success_of) { return '' }
  $path = Join-Path $queueRoot 'captures\queue\results.jsonl'
  if (-not (Test-Path -LiteralPath $path)) { return 'waiting for prerequisite jobs' }
  $records = @(Get-Content -LiteralPath $path | ForEach-Object {
    try { $_ | ConvertFrom-Json } catch { }
  })
  foreach ($required in @($job.requires_success_of)) {
    $last = @($records | Where-Object { $_.job -eq $required } | Select-Object -Last 1)
    if (-not $last.Count -or -not $last[0].valid -or $last[0].build_changed -or
        $last[0].outcome -notmatch '^Run ended: done\b') {
      return "waiting for successful prerequisite $required"
    }
    if ($currentBuild -and $last[0].build -ne $currentBuild) { return "prerequisite $required used another build" }
  }
  return ''
}
