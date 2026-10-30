# Offline only: synthetic receipts/binaries in TEMP; never a real inbox or game.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
$fake=Join-Path $env:TEMP ('bg3-child-auth-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $fake 'bin'),(Join-Path $fake 'captures/queue/authorizations') | Out-Null
Set-Content (Join-Path $fake '.queue-fake-root') synthetic
Set-Content (Join-Path $fake 'bin/VkLayer_bg3rtx.dll') synthetic-layer
Set-Content (Join-Path $fake 'bin/bg3rtx_core.dll') synthetic-core
$job=[pscustomobject]@{name='child';live_batch='test';timeout=10;args=@('-Foreground');patterns=@('fake');expect=@('fake')}
$build=Get-QueueBuildIdentity $fake
$runner="$PID|synthetic"
$grant=@{version=1;status='approved';user_steering='SYNTHETIC ONLY';steering_source='CPU test';expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');runner_session='test';jobs=@(@{hash=(Get-QueueJobHash $job $fake);tree=$fake;build=$build;runs=1})}
$grantPath=Join-Path $fake 'captures/queue/authorizations/test.json'
function WriteGrant {$grant | ConvertTo-Json -Depth 12 | Set-Content $grantPath}
WriteGrant
$count=0
function Check([bool]$condition,[string]$label){if(-not $condition){throw "FAIL $label"};$script:count++}
$oldContext=$env:BG3RTX_QUEUE_CONTEXT;$oldRoot=$env:BG3RTX_QUEUE_ROOT
try {
 $env:BG3RTX_QUEUE_ROOT=$fake
 $env:BG3RTX_QUEUE_CONTEXT=''
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'absent context rejected'
 Check ([bool](Get-QueuePermissionBlock $job $fake $fake $build test $runner -InFlight)) 'unconsumed receipt rejected'
 $statePath=Use-QueuePermission $job $fake $fake $build test $runner
 $good=New-QueueChildContext $job $fake $fake $build test $runner
 $env:BG3RTX_QUEUE_CONTEXT=$good
 Check (-not(Test-Path (Join-Path $fake 'captures/queue/USER_AWAY'))) 'no away marker manufactured'
 Check (-not(Get-QueueChildPermissionBlock $fake)) 'current consumed presented job accepted without away'
 Check (-not(Get-QueueLaunchProcessBlock @([pscustomobject]@{ProcessName='powershell';Id=1}))) 'claim starts with no game'
 foreach($name in @('bg3','bg3_dx11')){
  $appeared=@([pscustomobject]@{ProcessName=$name;Id=456})
  Check ([bool](Get-QueueLaunchProcessBlock $appeared)) "$name appeared after claim blocks launch"
 }
 # Assert the actual entry/setup/prelaunch ordering and absence of the old
 # automatic prior-run process kill, rather than only testing an unused helper.
 $source=Get-Content (Join-Path $repo 'scripts/auto-run.ps1') -Raw
 Check ($source -match 'Assert-QueueLaunchProcesses\s*\$game = @\(&') 'process guard is immediately before actual launch'
 Check ($source -match 'Assert-QueueLaunchProcesses\s*Remove-Item \$pidFile') 'process guard precedes stale PID cleanup and shared setup'
 Check ($source -notmatch '\$p\.CloseMainWindow\(\)') 'prior-run process is never closed by PID file'
 $child=Join-Path $fake 'validate.ps1'
 @'
param([string]$Repo,[string]$Tree,[switch]$Nested)
. (Join-Path $Repo 'scripts/queue-permission.ps1')
$block=Get-QueueChildPermissionBlock $Tree
if($block){Write-Output $block;exit 1}
if($Nested){& powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Repo $Repo -Tree $Tree;exit $LASTEXITCODE}
Write-Output 'valid child';exit 0
'@ | Set-Content $child
 $result=& powershell -NoProfile -ExecutionPolicy Bypass -File $child -Repo $repo -Tree $fake
 Check ($LASTEXITCODE -eq 0 -and "$result" -eq 'valid child') 'actual child validates live owner'
 $result=& powershell -NoProfile -ExecutionPolicy Bypass -File $child -Repo $repo -Tree $fake -Nested
 Check ($LASTEXITCODE -eq 0 -and "$result" -eq 'valid child') 'nested QA-style child inherits owner context'
 Check ([bool](Get-QueueChildPermissionBlock $env:TEMP)) 'different intended tree rejected'
 Set-Content (Join-Path $fake 'bin/bg3rtx_core.dll') changed
 Check ((Get-QueueChildPermissionBlock $fake) -match 'build changed') 'current candidate mutation rejected'
 Set-Content (Join-Path $fake 'bin/bg3rtx_core.dll') synthetic-core
 foreach($field in @('parent_start_ticks','parent_pid','session','runner','build','tree')){
  $bad=$good | ConvertFrom-Json
  switch($field){
   parent_start_ticks {$bad.parent_start_ticks='1'}
   parent_pid {$bad.parent_pid=2147483647;$bad.runner='2147483647|synthetic'}
   session {$bad.session='restarted'}
   runner {$bad.runner='foreign-runner'}
   build {$bad.build='old-build'}
   tree {$bad.tree=$env:TEMP}
  }
  $env:BG3RTX_QUEUE_CONTEXT=$bad | ConvertTo-Json -Depth 64 -Compress
  Check ([bool](Get-QueueChildPermissionBlock $fake)) "invalid $field rejected"
 }
 $env:BG3RTX_QUEUE_CONTEXT=$good
 $env:BG3RTX_QUEUE_ROOT=$env:TEMP
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'root mismatch rejected'
 $env:BG3RTX_QUEUE_ROOT=$fake
 $grant.status='revoked';WriteGrant
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'revocation rejected'
 $grant.status='approved';$grant.expires_utc=[datetimeoffset]::UtcNow.AddSeconds(-1).ToString('o');WriteGrant
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'expiry rejected'
 $grant.expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');WriteGrant
 Set-Content (Join-Path $fake 'captures/queue/USER_PLAYING') synthetic
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'playing overrides current context'
 Remove-Item -LiteralPath (Join-Path $fake 'captures/queue/USER_PLAYING')
 $state=Get-Content $statePath -Raw
 Remove-Item -LiteralPath $statePath
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'missing consumed state rejected'
 Set-Content $statePath $state
 $badState=$state | ConvertFrom-Json;$badState.used=@();$badState | ConvertTo-Json -Depth 8 | Set-Content $statePath
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'unconsumed job in existing state rejected'
 Set-Content $statePath $state
 Close-QueuePermission $statePath $runner
 Check ([bool](Get-QueueChildPermissionBlock $fake)) 'closed consumed context rejected'
 # Actual auto-run must refuse each mode BEFORE build/capture/launch code.
 $env:BG3RTX_QUEUE_CONTEXT=''
 foreach($mode in @('-Foreground','-NoActivate','-UnfocusedFG','-Background','-Session','-RealPresent')){
  $ErrorActionPreference='Continue'
  $result=& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'scripts/auto-run.ps1') -NoBuild $mode 2>&1
  $ErrorActionPreference='Stop'
  Check ($LASTEXITCODE -ne 0 -and "$result" -match 'Queue child context required') "actual auto-run $mode refuses direct launch"
 }
} finally {$env:BG3RTX_QUEUE_CONTEXT=$oldContext;$env:BG3RTX_QUEUE_ROOT=$oldRoot}
Write-Host "$count queue child authorization checks passed; synthetic scratch $fake"
