# Queue-owned read-only fixed SE probe. Run ONLY as auto-run.ps1's child helper.
# Never launches BG3, edits SE settings/mods/profiles, or activates a window.
param([Parameter(Mandatory)][ValidateSet('WaitConsole','Probe')][string]$Mode,
 [Parameter(Mandatory)][int]$GamePid,[Parameter(Mandatory)][long]$GameStartTicks,
 [Parameter(Mandatory)][int]$OwnerPid,[Parameter(Mandatory)][long]$OwnerStartTicks,[Parameter(Mandatory)][string]$PidFile,
 [Parameter(Mandatory)][string]$MarkerRoot,[Parameter(Mandatory)][string]$ResultPath)
$ErrorActionPreference='Stop'
$tree=Split-Path $PSScriptRoot
. (Join-Path $PSScriptRoot 'queue-permission.ps1')
function Assert-SEProbePermission([string]$tree,[string]$markerRoot) {
 $block=Get-QueueUserPlayingBlock $markerRoot $tree
 if($block){throw $block}
 $block=Get-QueueChildPermissionBlock $tree -ReceiptOnly
 if($block){throw $block}
}
$result=@{status='error';mode=$Mode;game_pid=$GamePid;context_restored=$false;input_clean=$false}
$script:inputUncertain=$false
try {
 $self=Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
 if([int]$self.ParentProcessId -ne $OwnerPid){throw 'SE helper is not the owned auto-run child'}
 function Assert-OwnedProbe {
  Assert-SEProbePermission $tree $MarkerRoot
  $game=Get-Process -Id $GamePid -ErrorAction Stop
  if($game.ProcessName -ne 'bg3' -or $game.StartTime.ToUniversalTime().Ticks -ne $GameStartTicks){throw 'SE game PID/start identity mismatch'}
  $record=Get-Content -LiteralPath $PidFile -Raw | ConvertFrom-Json
  if([int]$record.pid -ne $GamePid){throw 'SE game is not owned by this runner pid record'}
  $owner=Get-Process -Id $OwnerPid -ErrorAction Stop
  if($owner.StartTime.ToUniversalTime().Ticks -ne $OwnerStartTicks){throw 'SE runner owner identity changed'}
  $native=Get-CimInstance Win32_Process -Filter "ProcessId=$GamePid"
  if([int]$native.ParentProcessId -ne $OwnerPid){throw 'SE target is not a game launched by this runner'}
  if($env:BG3RTX_QUEUE_QUIT_FILE -and (Test-Path -LiteralPath $env:BG3RTX_QUEUE_QUIT_FILE)){throw 'SE probe queue cancelled'}
 }
 function Write-OwnedConsole([string]$command) {
  Assert-OwnedProbe
  $script:inputUncertain=$true
  [BG3RTX.SeConsoleProbe]::Write($command)
  $script:inputUncertain=$false
 }
 function Wait-FreshProbePrompt($before,[string]$expected,[int]$seconds) {
  $deadline=[datetime]::UtcNow.AddSeconds($seconds)
  do {
   Assert-OwnedProbe
   $current=[BG3RTX.SeConsoleProbe]::Capture()
   $context=[BG3RTX.SeConsoleProbe]::FreshPrompt($before,$current)
   if($context){
    if($expected -and $context -ne $expected){throw 'Unexpected newly produced SE console context'}
    return $context
   }
   Start-Sleep -Milliseconds 50
  }while([datetime]::UtcNow -lt $deadline)
  throw 'No newly produced SE prompt beyond pre-write cursor; abort owned run'
 }
 Assert-OwnedProbe
 Add-Type -Path (Join-Path $PSScriptRoot 'se-console-probe.cs')
 $deadline=[datetime]::UtcNow.AddSeconds(8);$attached=$false
 do {
  Assert-OwnedProbe
  if([BG3RTX.SeConsoleProbe]::Attach($GamePid)){$attached=$true;break}
  Start-Sleep -Milliseconds 50
 }while([datetime]::UtcNow -lt $deadline)
 if(-not $attached){throw 'Exact SE console unavailable after bounded wait'}
 if($Mode -eq 'WaitConsole'){$result.status='console_ready';$result.context_restored=$true;$result.input_clean=$true}else{
  $before=[BG3RTX.SeConsoleProbe]::Capture()
  Write-OwnedConsole "`n" # documented no-op; old prompt is not accepted
  $context=Wait-FreshProbePrompt $before '' 2
  $restoreNeeded=$context -eq 'S';$safeToRestore=$true
  try {
   if($restoreNeeded){
    $before=[BG3RTX.SeConsoleProbe]::Capture();$safeToRestore=$false
    Write-OwnedConsole "client`n"
    [void](Wait-FreshProbePrompt $before 'C' 2);$safeToRestore=$true
   }
   $nonce=[guid]::NewGuid().ToString('N');$result.nonce=$nonce
   $source=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'probes/se-blackbody.lua') -Raw
   $source=$source.Replace('__BG3RTX_PROBE_NONCE__',$nonce)
   $line=[BG3RTX.SeConsoleProbe]::SingleLine($source)
   $report=Join-Path $env:LOCALAPPDATA "Larian Studios/Baldur's Gate 3/Script Extender/bg3rtx/native_blackbody_probe_$nonce.json"
   $before=[BG3RTX.SeConsoleProbe]::Capture();$safeToRestore=$false
   Write-OwnedConsole ($line+"`n")
   [void](Wait-FreshProbePrompt $before 'C' 8);$safeToRestore=$true
   $deadline=[datetime]::UtcNow.AddSeconds(8)
   do {
    Assert-OwnedProbe
    if(Test-Path -LiteralPath $report){
     if((Get-Item -LiteralPath $report).Length -gt 1MB){throw 'SE probe report exceeded bounded size'}
     $raw=[IO.File]::ReadAllText($report)
     try{$data=$raw|ConvertFrom-Json}catch{$data=$null}
     if($data -and $data.nonce -eq $nonce){break}
    }
    Start-Sleep -Milliseconds 50
   }while([datetime]::UtcNow -lt $deadline)
   if(-not $data -or $data.nonce -ne $nonce){throw 'Native SE probe did not produce complete nonce-matched output'}
   [IO.File]::WriteAllText($ResultPath+'.raw.json',$raw)
   $result.status=$data.status;$result.records=@($data.lights).Count;$result.raw=$ResultPath+'.raw.json';$result.report=$report
   $known=0
   foreach($row in @($data.lights)){
    $valid=$true
    foreach($field in @('position','Color','Blackbody')){
     $entry=$row.$field
     if($entry.status -ne 'ok' -or @($entry.value).Count -ne 3){$valid=$false;continue}
     foreach($n in $entry.value){if($null -eq $n -or [double]::IsNaN([double]$n) -or [double]::IsInfinity([double]$n)){$valid=$false}}
    }
    foreach($field in @('Intensity','IntensityOffset','Gain','Radius')){
     $entry=$row.$field
     if($entry.status -ne 'ok' -or $null -eq $entry.value -or [double]::IsNaN([double]$entry.value) -or [double]::IsInfinity([double]$entry.value)){$valid=$false}
    }
    if($valid){$known++}
   }
   $result.known_blackbody_records=$known
   if($data.status -ne 'ok' -or @($data.lights).Count -eq 0 -or $known -ne @($data.lights).Count){throw 'SE probe is not a complete nonempty native-field S1 sample'}
  }finally{
   # A partial native write or missing completion prompt is uncertain input.
   # Never append a context command as a possible continuation of that input.
   # Helper failure makes auto-run abort/close only its owned game.
   if($safeToRestore -and -not $script:inputUncertain){
    if($restoreNeeded){
     $before=[BG3RTX.SeConsoleProbe]::Capture()
     Write-OwnedConsole "server`n"
     [void](Wait-FreshProbePrompt $before 'S' 2)
    }
    $result.context_restored=$true;$result.input_clean=$true
   }
  }
 }
}catch{$result.status='error';$result.error=$_.Exception.Message}
finally{
 if('BG3RTX.SeConsoleProbe' -as [type]){[BG3RTX.SeConsoleProbe]::Detach()}
 [IO.File]::WriteAllText($ResultPath,($result|ConvertTo-Json -Depth 6))
}
if($result.status -notin @('console_ready','ok')){exit 1}
