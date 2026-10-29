# Pure job/INI policy; importing this file never controls a window or starts a process.
function ConvertTo-QueueWindowJob($job) {
  $mode = [string]$job.window_mode
  if (-not $mode -and @($job.args) -notcontains '-NoActivate') { return $job }
  if ($mode -and $mode -ne 'visible_unfocused') { throw "Unsupported window_mode: $mode" }
  if ($job.command -or $job.phases) { throw 'visible_unfocused does not support command or live phases' }
  $copy = $job | ConvertTo-Json -Depth 32 | ConvertFrom-Json
  $args = @($copy.args | Where-Object { $_ -notin @('-Foreground','-NoActivate','-RealPresent') })
  $copy | Add-Member -Force NoteProperty args ($args + @('-RealPresent','-NoActivate'))
  $copy | Add-Member -Force NoteProperty set ((@($copy.set) -join ',') + ',fg=0,headless=0')
  $copy | Add-Member -Force NoteProperty session $false
  $copy | Add-Member -Force NoteProperty window_mode 'visible_unfocused'
  $copy
}

function Set-UnfocusedIniLines([string[]]$lines) {
  @($lines | Where-Object { $_ -notmatch '^\s*(fg|headless)\s*=' }) + @('fg=0','headless=0')
}
