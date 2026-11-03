# Pure input schedule: no window calls here, so timing/wrapping can be tested offline.
function Assert-SmoothRotationStep($step) {
  if (-not $step.frame_delta_pixels -or $step.frame_delta_pixels -notin @(1,2) -or
      ($step.frame_interval -and $step.frame_interval -ne 1)) { throw 'Smooth modes need 1 or 2 pixels every frame' }
}
function Get-CameraPlanSample($step, [double]$elapsed, [int]$width, [int]$height) {
  if ($step.turn_mode -and $step.turn_mode -notin @('legacy','hold_drag','centre_hold')) { throw 'Unknown turn_mode' }
  if ($step.turn_mode -in @('hold_drag','centre_hold')) { Assert-SmoothRotationStep $step }
  $active = $elapsed -ge $step.motion_start -and $elapsed -lt $step.motion_end
  $kind = if ($step.kind) { [string]$step.kind } elseif ($step.repeat_ms -gt 0) { 'pan' } else { 'static' }
  # Posted key repeat does not control held-key camera speed. A duty cycle does.
  if ($kind -eq 'pan' -and $step.key_period_ms) {
    if ($step.key_period_ms -le 0 -or $step.key_down_ms -le 0 -or $step.key_down_ms -gt $step.key_period_ms) {
      throw 'Key pulse needs 0 < key_down_ms <= key_period_ms'
    }
    $within = (($elapsed-$step.motion_start)*1000) % $step.key_period_ms
    $active = $active -and $within -lt $step.key_down_ms
  }
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
    $reset = -not $step.frame_delta_pixels -and $within -ge $dragSeconds
    $offset = if($reset) {0} else {[int][Math]::Floor($within*$step.pixels_per_second)}
    $x = [int][Math]::Floor($width / 2) + $step.direction * ($offset - [int][Math]::Floor($travel / 2))
    return @{ kind=$kind; active=($active -and -not $reset); reset=$reset; x=$x; y=[int][Math]::Floor($height/2);
              segment=$segment; direction=[int]$step.direction }
  }
  @{ kind=$kind; active=($active -and $kind -eq 'pan'); x=[int][Math]::Floor($width/2);
     y=[int][Math]::Floor($height/2); segment=0; direction=0 }
}

# Emit at most ONE transition per poll. The modulo sample describes desired
# motion, not proof that previously posted input has had time to be consumed.
# Retain the existing two-frame-at-18-fps settling budget AFTER each actual
# release and reset, including when the runner stalls across an entire reset.
function Get-RotationInputAction($state, $sample, [double]$now) {
  $gap = 2.0/18.0
  if (-not ($sample.kind -eq 'rotate' -and $sample.active)) {
    if ($state.stage -eq 'pressed') {
      $state.stage='released';$state.at=$now
      return @{kind='release';x=$state.x;y=$state.y}
    }
    if ($state.stage -eq 'positioned') {$state.stage='released';$state.at=$now}
    return $null
  }
  if ($state.stage -eq 'pressed') {
    if ($state.segment -ne $sample.segment) {
      $state.stage='released';$state.at=$now
      return @{kind='release';x=$state.x;y=$state.y}
    }
    if ($state.x -ne $sample.x -or $state.y -ne $sample.y) {
      $state.x=$sample.x;$state.y=$sample.y
      return @{kind='drag';x=$state.x;y=$state.y}
    }
    return $null
  }
  if ($state.stage -eq 'positioned' -and $state.segment -ne $sample.segment) {
    $state.stage='released';$state.at=$now
  }
  if ($now-$state.at -lt $gap) {return $null}
  if ($state.stage -eq 'released') {
    $state.stage='positioned';$state.at=$now
    $state.segment=$sample.segment;$state.x=$sample.x;$state.y=$sample.y
    return @{kind='reset';x=$state.x;y=$state.y}
  }
  if ($state.stage -eq 'positioned') {
    $state.stage='pressed';$state.at=$now
    return @{kind='press';x=$state.x;y=$state.y}
  }
  throw 'Invalid rotation input state'
}

# Frame-paced drag for capture plans. One small delta per newly observed rendered
# frame, never a wall-clock catch-up jump after a stalled runner/game.
function Get-FrameRotationSample($state, $sample, $step, [long]$frame, [int]$width) {
  if ($step.turn_mode -in @('hold_drag','centre_hold')) {
    Assert-SmoothRotationStep $step
    if ($state.label -ne $step.label) {
      $state.label=$step.label; $state.frame=$frame; $state.offset=0; $state.segment=0
    }
    if ($sample.active -and $frame -gt $state.frame) {
      # One observed frame, no catch-up after stalls. Never wrap/recentre held input.
      if ($step.turn_mode -eq 'hold_drag') { $state.offset += [int]$step.frame_delta_pixels }
      else { $state.offset=[int]$step.frame_delta_pixels }
    }
    $state.frame=[Math]::Max($state.frame,$frame)
    $sample.segment=0; $sample.reset=$false
    $sample.x=[int][Math]::Floor($width/2)+$step.direction*$state.offset
    # Fail closed instead of wrapping, clamping (which holds an offset), or leaving the client.
    if ($sample.x -lt 1 -or $sample.x -ge $width-1) { throw 'Smooth drag reached client edge; shorten phase' }
    return $sample
  }
  if (-not $step.frame_delta_pixels) { return $sample }
  if ($step.frame_delta_pixels -lt 1 -or $step.frame_delta_pixels -gt 2) { throw 'Frame drag needs 1 or 2 pixels' }
  if ($state.label -ne $step.label) {
    $state.label=$step.label; $state.frame=$frame; $state.offset=0; $state.segment=0
  }
  $interval=if ($step.frame_interval) { [int]$step.frame_interval } else { 1 }
  if ($interval -lt 1) { throw 'Frame interval must be positive' }
  if ($sample.active -and $frame -ge $state.frame+$interval) {
    $state.frame=$frame
    $state.offset += [int]$step.frame_delta_pixels
    $travel=[Math]::Max(8,[int][Math]::Floor($width/4))
    if ($state.offset -ge $travel) { $state.offset=0; $state.segment++ }
  }
  if (-not $sample.active) { $state.frame=[Math]::Max($state.frame,$frame) }
  $sample.segment=$state.segment
  $sample.x=[int][Math]::Floor($width/2)-[int][Math]::Floor($width/8)+$step.direction*$state.offset
  $sample
}

# This is the exact posted mouse protocol, shared by the runner and offline tests.
function Get-RotationMouseMessages($action) {
  if (-not $action) { return }
  $message=0x0200; $buttons=0
  switch ($action.kind) {
    press { $message=0x0207; $buttons=0x10 }
    drag { $buttons=0x10 }
    release { $message=0x0208 }
    reset { }
    default { throw 'Unknown rotation action' }
  }
  @{message=$message;buttons=$buttons;x=$action.x;y=$action.y}
}
