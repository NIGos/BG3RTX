# Pure input schedule: no window calls here, so timing/wrapping can be tested offline.
function Assert-SmoothRotationStep($step) {
  if (-not $step.frame_delta_pixels -or $step.frame_delta_pixels -notin @(1,2) -or
      ($step.frame_interval -and $step.frame_interval -ne 1)) { throw 'Smooth modes need 1 or 2 pixels every frame' }
}
function Get-CameraPlanSample($step, [double]$elapsed, [int]$width, [int]$height) {
  if ($step.turn_mode -and $step.turn_mode -notin @('legacy','hold_drag','centre_hold','keys')) { throw 'Unknown turn_mode' }
  if ($step.turn_mode -in @('hold_drag','centre_hold')) { Assert-SmoothRotationStep $step }
  $active = $elapsed -ge $step.motion_start -and $elapsed -lt $step.motion_end
  $kind = if ($step.kind) { [string]$step.kind } elseif ($step.repeat_ms -gt 0) { 'pan' } else { 'static' }
  if ($step.turn_mode -eq 'keys') {
    if ($kind -ne 'rotate' -or $step.direction -notin @(-1,1)) { throw 'Key rotation needs kind rotate and direction -1 or +1' }
    # Q/E remain held for the whole motion interval. No mouse samples, wrapping,
    # frame pacing or duty cycle: BG3 determines the angular speed of a held key.
    $scan = if ($step.direction -eq -1) { 0x10 } else { 0x12 }
    return @{kind='key_turn';active=$active;scan=$scan;direction=[int]$step.direction;
             x=[int][Math]::Floor($width/2);y=[int][Math]::Floor($height/2);segment=0}
  }
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

# Bundle13 plans encode build witnesses in wait_log. These are conditional:
# resident variants and push constants emit no new pipeline completion record.
function Test-CameraPlanBuildPattern([string]$pattern) {
  return $pattern -match 'generation |pipeline built|: built|surface Fresnel|read from the cache|cache: prefetched |pipelines 0'
}

# Only GI/water have reliable build-start tracking. Quiet records are replaced
# by reconciliation; state observations and other completions remain explicit.
function Test-CameraPlanReconciledPattern([string]$pattern) {
  return (Test-CameraPlanBuildPattern $pattern) -and
    ($pattern -match '^gi: |water.*: built|shaders|watch|read from the cache|cache: prefetched |pipelines 0')
}

function Get-CameraPlanMissing($step, [string]$text, [string]$freshText=$text) {
  if ($step.wait_log_fresh) {
    $cfg=[regex]::Match($freshText, '(?m)^.*\[cfg\] settings read[^\n]*')
    if (-not $cfg.Success) { return '[cfg] settings read after settings write' }
    $freshText=$freshText.Substring($cfg.Index)
    # Check reported keys, including numerical formatting (1 vs 1.00). Some
    # observation/CPU knobs are not included in the renderer's keys record.
    $keys=@([regex]::Matches($freshText, '(?m)^.*\[cfg\] keys:.*$'))
    if ($step.set) {
      if (-not $keys.Count) { return '[cfg] keys with requested values' }
      $values=@{}
      foreach ($pair in ($step.set -split ',')) {
        $kv=$pair -split '=',2
        if ($kv.Count -eq 2) { $values[$kv[0]]=$kv[1] }
      }
      $ack=$null
      foreach ($record in $keys) {
        $bad=@();$seen=0
        $ack=$null
        foreach ($key in $values.Keys) {
          $m=[regex]::Match($record.Value, '(?:^|\s)'+[regex]::Escape($key)+'=([^\s]+)')
          if ($m.Success) { $seen++ }
          if ($m.Success -and $m.Groups[1].Value -ne $values[$key]) {
            $actual=0.0;$wanted=0.0
            $culture=[Globalization.CultureInfo]::InvariantCulture
            if (-not ([double]::TryParse($m.Groups[1].Value, [Globalization.NumberStyles]::Float, $culture, [ref]$actual) -and
                      [double]::TryParse($values[$key], [Globalization.NumberStyles]::Float, $culture, [ref]$wanted) -and $actual -eq $wanted)) {
              $bad += "$key=$($values[$key])"
            }
          }
        }
        if ($seen -and -not $bad.Count) { $ack=$record }
      }
      # Keys never reported in the renderer's keys record (ab_off is a hotkey A/B, observation knobs)
      # cannot be acknowledged there: the settings read is their acknowledgement.
      $reported=@($values.Keys | Where-Object { $k=$_; @($keys | Where-Object { $_.Value -match ('(?:^|\s)'+[regex]::Escape($k)+'=') }).Count })
      if ($null -eq $ack -and $reported.Count) { return '[cfg] keys: '+($bad -join ', ') }
      # Start/completion reconciliation below includes builds between settings
      # read and keys, and outstanding builds from earlier phases.
    }
    $pending=@{}; $historical=@{}; $afterAck=$false
    $explicitOnly=-not @($step.wait_log | Where-Object { Test-CameraPlanBuildPattern $_ }).Count
    $affected=@($step.wait_log | Where-Object {
      (Test-CameraPlanBuildPattern $_) -and $_ -notmatch 'shaders|watch|surface Fresnel' -and
      -not $_.Contains('gi/[^ ]+')
    })
    foreach ($line in ($text -split "`n")) {
      if ($line.TrimEnd("`r") -eq $cfg.Value.TrimEnd("`r")) { $afterAck=$true }
      $id=$null;$completion=$null;$done=$false
      if ($line -match 'gi: (\S+) building in the background') { $id=$Matches[1];$completion='gi: '+$id+' generation ' }
      elseif ($line -match 'gi: (\S+) generation ') { $id=$Matches[1];$done=$true }
      elseif ($line -match '(\S+\.hlsl[^ ]*) (\w+): building in the background') { $id=$Matches[1]+' '+$Matches[2];$completion='[water] '+$Matches[1]+' '+$Matches[2]+': built' }
      elseif ($line -match '\[water\] (\S+\.hlsl[^ ]*) (\w+): built') { $id=$Matches[1]+' '+$Matches[2];$done=$true }
      elseif ($line -match '\[rrpost\] (rr_post[^ ]*\.hlsl:\w+) pipeline (built|building|creation failed)') {
        $id='rrpost '+$Matches[1];$completion='[rrpost] '+$Matches[1]+' pipeline built'
        $done=$Matches[2] -eq 'built'
      }
      if ($done) {
        $pending.Remove($id);$historical.Remove($id)
        # Older starts may belong to abandoned alternatives (_k, hit_sun vs
        # hit_sun_em). A selected completion is authoritative for that mapped
        # pass family. New starts always require their exact completion.
        foreach ($old in @($historical.Keys)) {
          if (@($historical[$old] | Where-Object { $line -match $_ }).Count) {
            $pending.Remove($old);$historical.Remove($old)
          }
        }
      } elseif ($id) {
        $families=@($affected | Where-Object { $completion -match $_ })
        if ($afterAck -or $families.Count -or $explicitOnly) {
          $pending[$id]=$completion
          if (-not $afterAck) { $historical[$id]=$families }
        }
      }
    }
    foreach ($missing in $pending.Values) { $missing }
  }
  foreach ($pattern in @($step.wait_log)) {
    if ($step.wait_log_fresh -and (Test-CameraPlanReconciledPattern $pattern)) { continue }
    $source=if ($pattern -in @($step.wait_log_fresh) -or ($step.wait_log_fresh -and $pattern -match 'surface Fresnel')) {$freshText} else {$text}
    if (-not @($source -split "`n" | Where-Object { $_ -match $pattern }).Count) { $pattern }
  }
}

function Test-CameraPlanReady($step, [string]$text, [string]$freshText=$text) {
  return @(Get-CameraPlanMissing $step $text $freshText).Count -eq 0
}

# Central list of phase fields consumed by auto-run and the camera samplers.
# Fail offline on typos instead of silently accepting an ignored instruction.
function Assert-CameraPlanFields($step) {
  $registry=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'camera-plan-fields.json') -Raw | ConvertFrom-Json
  $allowed=@($registry.runner)+@($registry.analysis_only)
  $names=if ($step -is [System.Collections.IDictionary]) { @($step.Keys) } else { @($step.PSObject.Properties.Name) }
  foreach ($name in $names) {
    if ($name -notin $allowed) { throw "Unknown camera plan field: $name" }
  }
  if ($step.wait_log_fresh) {
    foreach ($pattern in @($step.wait_log_fresh)) {
      if ($pattern -notin @($step.wait_log)) { throw 'wait_log_fresh must be a subset of wait_log' }
    }
  }
  if ($step.wait_log) {
    if (-not ($step.wait_log_timeout -gt 0) -or [double]::IsInfinity([double]$step.wait_log_timeout)) {
      throw 'wait_log requires a finite positive wait_log_timeout'
    }
    foreach ($pattern in @($step.wait_log)) {
      if (-not ($pattern -is [string]) -or -not $pattern.Trim()) { throw 'wait_log requires nonempty regex strings' }
      [void][regex]::new($pattern)
    }
  } elseif ($step.wait_log_timeout) { throw 'wait_log_timeout requires wait_log' }
  if ('recenter' -in $names -and -not ($step.recenter -is [bool])) { throw 'recenter requires a boolean' }
  if ('reload_selected' -in $names -and -not ($step.reload_selected -is [bool])) { throw 'reload_selected requires a boolean' }
}
