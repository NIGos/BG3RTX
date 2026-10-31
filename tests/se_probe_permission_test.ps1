# CPU only: test the ACTUAL probe permission function against synthetic receipts.
# No helper entry point, console transport, installed files or real queue is used.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'scripts/se-console-probe.ps1'),[ref]$null,[ref]$null)
$fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Assert-SEProbePermission'},$true)
if(-not $fn){throw 'Actual probe permission boundary missing'}
. ([scriptblock]::Create($fn.Extent.Text))
$scratch=Join-Path $env:TEMP ('bg3-se-permission-'+[guid]::NewGuid().ToString('N'))
$main=Join-Path $scratch 'main';$private=Join-Path $scratch 'private';$queue=Join-Path $scratch 'queue'
New-Item -ItemType Directory -Force $main,$queue | Out-Null
& git -C $main init --quiet
if($LASTEXITCODE){throw 'scratch git init failed'}
& git -C $main -c user.name=Synthetic -c user.email=synthetic@example.invalid commit --quiet --allow-empty -m synthetic
if($LASTEXITCODE){throw 'scratch git commit failed'}
& git -C $main worktree add --quiet --detach $private
if($LASTEXITCODE){throw 'scratch worktree failed'}
foreach($path in @($main,$private,$queue)){New-Item -ItemType Directory -Force (Join-Path $path 'captures/queue/authorizations') | Out-Null}
New-Item -ItemType Directory -Force (Join-Path $private 'bin') | Out-Null
Set-Content (Join-Path $private 'bin/VkLayer_bg3rtx.dll') synthetic
Set-Content (Join-Path $private 'bin/bg3rtx_core.dll') synthetic
$job=[pscustomobject]@{name='probe';live_batch='synthetic';timeout=10;patterns=@('fake');expect=@('fake')}
$build=Get-QueueBuildIdentity $private;$runner="$PID|synthetic"
$grant=@{version=1;status='approved';runner_session='synthetic';user_steering='SYNTHETIC ONLY';steering_source='CPU test';expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');jobs=@(@{hash=(Get-QueueJobHash $job $private);tree=$private;build=$build;runs=1})}
$grantPath=Join-Path $queue 'captures/queue/authorizations/synthetic.json'
function WriteGrant {$grant | ConvertTo-Json -Depth 12 | Set-Content $grantPath}
WriteGrant
$statePath=Use-QueuePermission $job $queue $private $build synthetic $runner
$oldRoot=$env:BG3RTX_QUEUE_ROOT;$oldContext=$env:BG3RTX_QUEUE_CONTEXT;$count=0
function Expect([bool]$allowed,[string]$label){
 $ok=$true;try{Assert-SEProbePermission $private $queue}catch{$ok=$false}
 if($ok -ne $allowed){throw "FAIL $label"};$script:count++
}
try{
 $env:BG3RTX_QUEUE_ROOT=$queue
 $good=New-QueueChildContext $job $queue $private $build synthetic $runner
 $env:BG3RTX_QUEUE_CONTEXT=$good
 Expect $true 'Consumed finite receipt accepted'
 $env:BG3RTX_QUEUE_CONTEXT='';Expect $false 'Missing context rejected'
 $env:BG3RTX_QUEUE_CONTEXT=$good
 foreach($path in @($main,$private,$queue)){
  $marker=Join-Path $path 'captures/queue/USER_PLAYING'
  Set-Content $marker synthetic
  Expect $false 'Each canonical marker blocks existing permission'
  Remove-Item -LiteralPath $marker
  Expect $true 'Marker removal rechecked'
 }
 $grant.status='revoked';WriteGrant;Expect $false 'Revoked receipt blocks'
 $grant.status='approved';$grant.expires_utc=[datetimeoffset]::UtcNow.AddSeconds(-1).ToString('o');WriteGrant
 Expect $false 'Expired receipt blocks'
 $grant.expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');WriteGrant
 Expect $true 'Valid permission restored'
 Close-QueuePermission $statePath $runner
 Expect $false 'Closed consumed state blocks'
}finally{$env:BG3RTX_QUEUE_ROOT=$oldRoot;$env:BG3RTX_QUEUE_CONTEXT=$oldContext}
Write-Output "$count actual SE probe permission checks PASS; synthetic scratch $scratch"
