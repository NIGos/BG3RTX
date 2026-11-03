# Pure evidence parser. Input must contain only bytes written after the load request.
function Test-SelectedSaveReloadEvidence([string]$Text) {
  $loading=$false; $steady=0
  foreach($line in ($Text -split "`n")) {
    if($line -match '\[auto\] present \d+: (\d+) passes, (\d+) draws in the last frame') {
      $passes=[int]$Matches[1]; $draws=[int]$Matches[2]
      if($passes -lt 10) { $loading=$true; $steady=0 }
      elseif($loading -and $passes -ge 40 -and $draws -ge 400) { $steady++ }
      else { $steady=0 }
      # Samples are 300 presents apart. Two stable post-load samples prove a
      # full 300-present scene interval, independent of the one-shot ready flag.
      if($loading -and $steady -ge 2) { return $true }
    }
  }
  return $false
}
