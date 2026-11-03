# Actual test-queue -> Start-Process Windows PowerShell path, synthetic ONLY.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
$fake=Join-Path $env:TEMP ('bg3-child-startprocess-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $fake 'bin'),(Join-Path $fake 'scripts'),(Join-Path $fake 'captures/queue/pending'),(Join-Path $fake 'captures/queue/authorizations') | Out-Null
Set-Content (Join-Path $fake '.queue-fake-root') synthetic
Set-Content (Join-Path $fake 'bin/VkLayer_bg3rtx.dll') synthetic-layer
Set-Content (Join-Path $fake 'bin/bg3rtx_core.dll') synthetic-core
foreach($file in @('test-queue.ps1','queue-permission.ps1','queue-window-mode.ps1')){Copy-Item -LiteralPath (Join-Path $repo "scripts/$file") -Destination (Join-Path $fake "scripts/$file")}
# The real queue starts this stand-in through Start-Process powershell, exactly
# where R2 failed. It cannot launch a game and deliberately forbids Get-FileHash.
@'
param([string]$Root,[int]$Traces,[int]$TimeoutSec)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'queue-permission.ps1')
$hashAvailable=[bool](Get-Command Get-FileHash -ErrorAction SilentlyContinue)
Write-Output "Inherited PS5 Get-FileHash available=$hashAvailable"
function Get-FileHash {throw 'Get-FileHash must not be required by queue authorization'}
$block=Get-QueueChildPermissionBlock $Root
if($block){throw $block}
if($env:BG3RTX_QUEUE_CONTEXT){throw 'Full job context still passed through environment'}
if(-not $env:BG3RTX_QUEUE_CONTEXT_FILE){throw 'Context file path missing'}
$context=Get-Content -LiteralPath $env:BG3RTX_QUEUE_CONTEXT_FILE -Raw
if($context.Length -le 32767){throw 'Combined-job regression did not exceed environment value limit'}
$fixture=($context | ConvertFrom-Json).job.fixture_job
if($fixture.name -ne 'combined-Water-Queens-House'){throw 'Combined job payload did not survive child transport'}
$phase=Join-Path $Root 'phase-input.txt'
Set-Content $phase 'phase fixture'
$phaseJob=[pscustomobject]@{phases='phase-input.txt'}
if((Get-QueueJobHash $phaseJob $Root) -notmatch '^[a-f0-9]{64}$'){throw 'phase hash failed'}
Write-Output "CrossPS child validated: PS=$($PSVersionTable.PSVersion.Major) module-independent hash"
Write-Output 'Run ended: done'
'@ | Set-Content (Join-Path $fake 'scripts/test-queue-fake.ps1')
$job=[pscustomobject]@{name='startprocess';live_batch='synthetic';timeout=30;traces=0;args=@('-Foreground','-RealPresent');patterns=@('synthetic');expect_runner=@('CrossPS child validated: PS=5 module-independent hash','Run ended: done')}
# Preserve the actual oversized job as inert data: only this stand-in runs.
$fixture=Get-Content (Join-Path $repo 'tools/live_batches/combined_20261103/jobs/combined-Water-Queens-House.json') -Raw | ConvertFrom-Json
$job | Add-Member fixture_job $fixture
$build=Get-QueueBuildIdentity $fake
$grant=@{version=1;status='approved';user_steering='SYNTHETIC CPU ONLY';steering_source='queue child Start-Process regression';expires_utc=[datetimeoffset]::UtcNow.AddMinutes(3).ToString('o');runner_session='crossps';jobs=@(@{hash=(Get-QueueJobHash $job $fake);tree=$fake;build=$build;runs=1})}
$grant | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $fake 'captures/queue/authorizations/synthetic.json')
$job | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $fake 'captures/queue/pending/startprocess.json')
$queueShell=if(Get-Command pwsh -ErrorAction SilentlyContinue){'pwsh'}else{'powershell'}
& $queueShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fake 'scripts/test-queue.ps1') -DryRun -Root $fake -PermissionSession crossps
if($LASTEXITCODE -ne 0){throw 'actual synthetic queue failed'}
$rows=@(Get-Content (Join-Path $fake 'captures/queue/results.jsonl') | ConvertFrom-Json)
if($rows.Count -ne 1 -or -not $rows[0].valid -or $rows[0].outcome -notmatch '^Run ended: done'){throw 'Start-Process child validation was not successful'}
if(Test-Path (Join-Path $fake 'captures/queue/USER_AWAY')){throw 'unexpected away marker'}
if(@(Get-ChildItem -LiteralPath (Join-Path $fake 'captures/queue') -Filter 'child-context.*.json').Count){throw 'Context files leaked after queue shutdown'}
Write-Host "Actual $queueShell queue -> Start-Process PS5 child accepted oversized combined scope without Get-FileHash; scratch $fake"
