# Read-only launch policy. Never create USER_AWAY on behalf of the user.
function Get-QueuePermissionBlock($job, [string]$queueRoot) {
  if (Test-Path -LiteralPath (Join-Path $queueRoot 'captures\queue\USER_PLAYING')) { return 'USER_PLAYING exists' }
  if ((@($job.args) -contains '-Foreground' -or @($job.args) -contains '-NoActivate' -or $job.window_mode -eq 'visible_unfocused') -and
      -not (Test-Path -LiteralPath (Join-Path $queueRoot 'captures\queue\USER_AWAY'))) {
    return 'presented foreground/unfocused job waiting for USER_AWAY'
  }
  return ''
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
