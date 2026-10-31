# CPU only: independent TEMP repository/worktree; no real queue or game.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
$scratch=Join-Path $env:TEMP ('bg3-shared-marker-'+[guid]::NewGuid().ToString('N'))
$main=Join-Path $scratch 'main';$private=Join-Path $scratch 'private';$queue=Join-Path $scratch 'queue'
New-Item -ItemType Directory -Force $main,$queue | Out-Null
& git -C $main init --quiet
if($LASTEXITCODE){throw 'scratch git init failed'}
& git -C $main -c user.name=Synthetic -c user.email=synthetic@example.invalid commit --quiet --allow-empty -m synthetic
if($LASTEXITCODE){throw 'scratch git commit failed'}
& git -C $main worktree add --quiet --detach $private
if($LASTEXITCODE){throw 'scratch worktree failed'}
foreach($path in @($main,$private,$queue)) {New-Item -ItemType Directory -Force (Join-Path $path 'captures/queue/authorizations') | Out-Null}
New-Item -ItemType Directory -Force (Join-Path $private 'bin') | Out-Null
Set-Content (Join-Path $private 'bin/VkLayer_bg3rtx.dll') synthetic
Set-Content (Join-Path $private 'bin/bg3rtx_core.dll') synthetic
$count=0
function Check([bool]$condition,[string]$label){if(-not $condition){throw "FAIL $label"};$script:count++}
$job=[pscustomobject]@{name='marker';live_batch='synthetic';timeout=10;patterns=@('fake');expect=@('fake')}
$build=Get-QueueBuildIdentity $private
$runner="$PID|synthetic"
$grant=@{version=1;status='approved';runner_session='synthetic';user_steering='SYNTHETIC ONLY';steering_source='CPU test';expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');jobs=@(@{hash=(Get-QueueJobHash $job $private);tree=$private;build=$build;runs=1})}
$grant | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $queue 'captures/queue/authorizations/synthetic.json')
Check (-not(Get-QueueUserPlayingBlock $private)) 'linked startup initially clear'
Check (-not(Get-QueueUserPlayingBlock $queue $private)) 'separate non-git queue initially clear'
Check (-not(Get-QueuePermissionBlock $job $queue $private $build synthetic $runner)) 'prelaunch permit initially clear'
$state=Use-QueuePermission $job $queue $private $build synthetic $runner
$oldRoot=$env:BG3RTX_QUEUE_ROOT;$oldContext=$env:BG3RTX_QUEUE_CONTEXT
try {
 $env:BG3RTX_QUEUE_ROOT=$queue
 $env:BG3RTX_QUEUE_CONTEXT=New-QueueChildContext $job $queue $private $build synthetic $runner
 Check (-not(Get-QueueChildPermissionBlock $private -ReceiptOnly)) 'in-flight child initially clear'
 foreach($path in @($main,$private,$queue)) {
  # Resolution above is already cached. Only file existence changes now.
  $marker=Join-Path $path 'captures/queue/USER_PLAYING'
  Set-Content $marker synthetic
  Set-Content (Join-Path $queue 'captures/queue/USER_AWAY') synthetic
  Check ((Get-QueueUserPlayingBlock $queue $private) -match 'USER_PLAYING exists') 'each marker overrides away after cache'
  Check ((Get-QueuePermissionBlock $job $queue $private $build synthetic $runner -InFlight) -match 'USER_PLAYING exists') 'permission poll rejects marker'
  Check ((Get-QueueChildPermissionBlock $private -ReceiptOnly) -match 'USER_PLAYING exists') 'child poll rejects marker'
  Remove-Item -LiteralPath $marker
  Check (-not(Get-QueueChildPermissionBlock $private -ReceiptOnly)) 'marker removal observed without stale cache'
 }
 Check (-not(Get-QueueUserPlayingBlock $queue)) 'non-git queue does not inherit unrelated repository'
 Set-Content (Join-Path $main 'captures/queue/USER_PLAYING') synthetic
 Check ((Get-QueueUserPlayingBlock $private) -match 'USER_PLAYING exists') 'private-root startup sees main marker'
 Check (-not(Get-QueueUserPlayingBlock $queue)) 'non-git scratch root remains isolated'
 $runnerSource=Get-Content (Join-Path $repo 'scripts/test-queue.ps1') -Raw
 $childSource=Get-Content (Join-Path $repo 'scripts/auto-run.ps1') -Raw
 Check ([regex]::Matches($runnerSource,'Get-QueueUserPlayingBlock \$root').Count -eq 2) 'runner startup and runtime use helper'
 Check ([regex]::Matches($childSource,'Get-QueueUserPlayingBlock \$markerRoot \$root').Count -eq 2) 'child startup and live loop use helper'
 Check ($childSource -notmatch '\$playingMarker') 'child has no frozen single marker path'
} finally {$env:BG3RTX_QUEUE_ROOT=$oldRoot;$env:BG3RTX_QUEUE_CONTEXT=$oldContext}
Write-Host "$count shared USER_PLAYING checks passed; scratch retained at $scratch"
