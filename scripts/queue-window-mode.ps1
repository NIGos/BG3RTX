# Pure job/INI policy; importing this file never controls a window or starts a process.
function ConvertTo-QueueWindowJob($job) {
  $mode = [string]$job.window_mode
  if (-not $mode -and @($job.args) -notcontains '-NoActivate') { return $job }
  if ($mode -and $mode -notin @('visible_unfocused','visible_unfocused_fg')) { throw "Unsupported window_mode: $mode" }
  if ($job.command -or $job.phases) { throw 'visible_unfocused does not support command or live phases' }
  $copy = $job | ConvertTo-Json -Depth 32 | ConvertFrom-Json
  $args = @($copy.args | Where-Object { $_ -notin @('-Foreground','-NoActivate','-RealPresent') })
  $extra = if($mode -eq 'visible_unfocused_fg') { @('-UnfocusedFG') } else { @() }
  $copy | Add-Member -Force NoteProperty args ($args + @('-RealPresent','-NoActivate') + $extra)
  $forced = if($mode -eq 'visible_unfocused_fg') { ',headless=0' } else { ',fg=0,headless=0' }
  $copy | Add-Member -Force NoteProperty set ((@($copy.set) -join ',') + $forced)
  $copy | Add-Member -Force NoteProperty session $false
  $copy | Add-Member -Force NoteProperty window_mode $(if($mode -eq 'visible_unfocused_fg'){$mode}else{'visible_unfocused'})
  $copy
}

function Set-UnfocusedIniLines([string[]]$lines, [switch]$PreserveFG) {
  if ($PreserveFG) { return @($lines | Where-Object { $_ -notmatch '^\s*headless\s*=' }) + @('headless=0') }
  @($lines | Where-Object { $_ -notmatch '^\s*(fg|headless)\s*=' }) + @('fg=0','headless=0')
}
