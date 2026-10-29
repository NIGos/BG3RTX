# Pure input schedule: no window calls here, so timing/wrapping can be tested offline.
function Get-CameraPlanSample($step, [double]$elapsed, [int]$width, [int]$height) {
  $active = $elapsed -ge $step.motion_start -and $elapsed -lt $step.motion_end
  $kind = if ($step.kind) { [string]$step.kind } elseif ($step.repeat_ms -gt 0) { 'pan' } else { 'static' }
  if ($kind -eq 'rotate') {
    if ($step.pixels_per_second -le 0 -or $step.direction -notin @(-1,1)) { throw 'Rotation needs positive speed and direction -1 or +1' }
    $travel = [Math]::Max(8, [int][Math]::Floor($width / 4))
    # Allow button-up/reset events to be consumed before the next drag. Two
    # frames at the gate's minimum 18 fps avoid a one-frame reverse wrap delta.
    $resetSeconds = 2.0/18.0
    $dragSeconds = [double]$travel/$step.pixels_per_second
    $progress = [Math]::Max(0.0,$elapsed-$step.motion_start)
    $segment = [int64][Math]::Floor($progress/($dragSeconds+$resetSeconds))
    $within = $progress-$segment*($dragSeconds+$resetSeconds)
    $reset = $within -ge $dragSeconds
    $offset = if($reset) {0} else {[int][Math]::Floor($within*$step.pixels_per_second)}
    $x = [int][Math]::Floor($width / 2) + $step.direction * ($offset - [int][Math]::Floor($travel / 2))
    return @{ kind=$kind; active=($active -and -not $reset); reset=$reset; x=$x; y=[int][Math]::Floor($height/2);
              segment=$segment; direction=[int]$step.direction }
  }
  @{ kind=$kind; active=($active -and $kind -eq 'pan'); x=[int][Math]::Floor($width/2);
     y=[int][Math]::Floor($height/2); segment=0; direction=0 }
}
