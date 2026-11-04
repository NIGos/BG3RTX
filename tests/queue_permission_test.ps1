# Offline only: synthetic receipts live under TEMP, never a watched real inbox.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts/queue-permission.ps1')
. (Join-Path $repo 'scripts/queue-window-mode.ps1')
$root=Join-Path $env:TEMP ('bg3-queue-auth-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $root 'bin'),(Join-Path $root 'captures/queue/authorizations'),(Join-Path $root 'captures/queue/pending') | Out-Null
Set-Content (Join-Path $root '.queue-fake-root') 'synthetic'
Set-Content (Join-Path $root 'bin/VkLayer_bg3rtx.dll') 'fake layer: scene ready at present'
Set-Content (Join-Path $root 'bin/bg3rtx_core.dll') 'fake core'
$count=0
function Check([bool]$x,[string]$label){if(-not $x){throw "FAIL $label"};$script:count++}
$job=[pscustomobject]@{name='test';live_batch='fake';repeat=1;command='echo Run ended: done';patterns=@('fake');expect=@('fake')}
$build=Get-QueueBuildIdentity $root
function Block {Get-QueuePermissionBlock $job $root $root (Get-QueueBuildIdentity $root) 'session1' 'runner1'}
Check ([bool](Block)) 'missing receipt rejects command'
Set-Content (Join-Path $root 'captures/queue/USER_AWAY') 'synthetic'
$old=[pscustomobject]@{window_mode='visible_unfocused_fg';args=@('-UnfocusedFG')}
Check ([bool](Get-QueuePermissionBlock $old $root)) 'old background/FG exception rejected'
$path=Join-Path $root 'captures/queue/authorizations/fake.json'
$grant=@{version=1;status='approved';runner_session='session1';user_steering='SYNTHETIC ONLY';steering_source='fake test';expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');jobs=@(@{hash=(Get-QueueJobHash $job $root);tree=$root;build=$build;runs=1})}
function SaveGrant {$grant | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path}
SaveGrant;Check (-not(Block)) 'exact finite grant permits'
$grant.jobs[0].runs=1.5;SaveGrant;Check ([bool](Block)) 'fractional run budget rejected';$grant.jobs[0].runs=1
$grant.expires_utc=[datetimeoffset]::UtcNow.AddSeconds(60).ToString('o');SaveGrant;Check ([bool](Block)) 'run exceeding window rejected';$grant.expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');SaveGrant
$grant.status='revoked';SaveGrant;Check ([bool](Block)) 'revoked rejected';$grant.status='approved'
$grant.expires_utc=[datetimeoffset]::UtcNow.AddMinutes(-1).ToString('o');SaveGrant;Check ([bool](Block)) 'expired rejected';$grant.expires_utc=[datetimeoffset]::UtcNow.AddHours(1).ToString('o');SaveGrant
$job.command='echo other';Check ([bool](Block)) 'different command rejected';$job.command='echo Run ended: done'
Set-Content (Join-Path $root 'bin/bg3rtx_core.dll') 'changed';Check ([bool](Block)) 'changed build rejected';Set-Content (Join-Path $root 'bin/bg3rtx_core.dll') 'fake core'
Check ([bool](Get-QueuePermissionBlock $job $root $env:TEMP $build 'session1' 'runner1')) 'different tree rejected'
Check ([bool](Get-QueuePermissionBlock $job $root $root $build 'other-session' 'runner1')) 'session restart rejected'
Set-Content (Join-Path $root 'captures/queue/USER_PLAYING') 'synthetic';Check ((Block) -match 'USER_PLAYING') 'playing overrides grant';Remove-Item -LiteralPath (Join-Path $root 'captures/queue/USER_PLAYING')
$state=Use-QueuePermission $job $root $root $build 'session1' 'runner1'
Check ([bool](Block)) 'consumed run rejects replay'
Check (-not(Get-QueuePermissionBlock $job $root $root $build 'session1' 'runner1' -InFlight)) 'active last run retains scope until completion'
Check ([bool](Get-QueuePermissionBlock $job $root $root $build 'session1' 'runner2' -InFlight)) 'same-session process restart rejected'
Close-QueuePermission $state 'runner1';Check ([bool](Get-QueuePermissionBlock $job $root $root $build 'session1' 'runner1' -InFlight)) 'closed grant rejected'
# Real existing runner, fake execution only: command, single and reused session.
$runRoot=Join-Path $root 'runner';New-Item -ItemType Directory -Force (Join-Path $runRoot 'bin'),(Join-Path $runRoot 'captures/queue/pending'),(Join-Path $runRoot 'captures/queue/authorizations'),(Join-Path $runRoot 'docs/live-test-drafts/M0') | Out-Null
Set-Content (Join-Path $runRoot '.queue-fake-root') 'synthetic'
Set-Content (Join-Path $runRoot 'bin/VkLayer_bg3rtx.dll') 'fake layer: scene ready at present'
Set-Content (Join-Path $runRoot 'bin/bg3rtx_core.dll') 'fake core'
Set-Content (Join-Path $runRoot 'bg3rtx_user.ini') 'USER_PREFERENCE=preserve'
$jobs=@(
 [pscustomobject]@{name='a-command';live_batch='batch';command='echo Run ended: done';traces=0;patterns=@('fake');expect_runner=@('Run ended: done')},
 [pscustomobject]@{name='b-single';live_batch='batch';traces=0;timeout=10;patterns=@('fake');expect=@('fake')},
 [pscustomobject]@{name='c-session';live_batch='batch';session=$true;set='gi=2';traces=0;warmup=0;hold=1;timeout=10;patterns=@('settings read');expect=@('settings read')},
 [pscustomobject]@{name='d-session';live_batch='batch';session=$true;set='gi=3';traces=0;warmup=0;hold=1;timeout=10;patterns=@('settings read');expect=@('settings read')})
$order=[datetime]::UtcNow.AddMinutes(-5)
foreach($j in $jobs){$file=Join-Path $runRoot "captures/queue/pending/$($j.name).json";$j | ConvertTo-Json | Set-Content $file;(Get-Item $file).LastWriteTimeUtc=($order=$order.AddSeconds(1))}
# Bad/unauthorized oldest inbox entries must not starve authorized work.
$blocked=Join-Path $runRoot 'captures/queue/pending/0-blocked.json'
Set-Content $blocked '{"name":"blocked","command":"SHOULD NEVER RUN"}'
$malformed=Join-Path $runRoot 'captures/queue/pending/0-malformed.json'
Set-Content $malformed '{not json'
(Get-Item $blocked).LastWriteTimeUtc=$order.AddMinutes(-2)
(Get-Item $malformed).LastWriteTimeUtc=$order.AddMinutes(-1)
Set-Content (Join-Path $runRoot 'docs/live-test-drafts/M0/unauthorized.json') '{"command":"SHOULD NEVER RUN"}'
$build=Get-QueueBuildIdentity $runRoot
$grant.jobs=@($jobs | ForEach-Object {@{hash=(Get-QueueJobHash $_ $runRoot);tree=$runRoot;build=$build;runs=1}});$grant.runner_session='fake-runner'
$grant | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $runRoot 'captures/queue/authorizations/batch.json')
$env:BG3RTX_FAKE_LOAD_SEC='.1'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'scripts/test-queue.ps1') -DryRun -Root $runRoot -PermissionSession fake-runner
Check ($LASTEXITCODE -eq 0) 'actual fake runner exits cleanly'
$records=@(Get-Content (Join-Path $runRoot 'captures/queue/results.jsonl') | ConvertFrom-Json)
Check ($records.Count -eq 6) 'four runs and two terminal refusals recorded'
Check (@($records | Where-Object {$_.outcome -match '^REFUSED'}).Count -eq 2) 'permission and malformed refusals recorded'
$records=@($records | Where-Object {$_.outcome -notmatch '^REFUSED'})
Check (-not(Test-Path $blocked) -and -not(Test-Path $malformed)) 'terminal refusals exit pending while later authorized jobs run'
Check ($records[2].session -and ($records[2].session -split '#')[0] -eq ($records[3].session -split '#')[0]) 'same session reuse exercised'
Check ((Get-Content (Join-Path $runRoot 'bg3rtx_user.ini') -Raw).Trim() -eq 'USER_PREFERENCE=preserve') 'user settings preserved'
Check (-not((Get-Content (Join-Path $runRoot 'captures/auto.ini') -Raw) -match '(?m)^gi=[23]$')) 'session settings restored'
Check (Test-Path (Join-Path $runRoot 'docs/live-test-drafts/M0/unauthorized.json')) 'nonwatched draft untouched'
Check (@(Get-ChildItem (Join-Path $runRoot 'captures/queue/done') -Filter '*.json').Count -eq 6) 'refused jobs archived and draft untouched'
# Re-delivery after runner exit cannot reuse the consumed grant/session.
$replay=Join-Path $runRoot 'captures/queue/pending/replay.json';$jobs[0] | ConvertTo-Json | Set-Content $replay
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'scripts/test-queue.ps1') -DryRun -Root $runRoot -PermissionSession fake-runner
Check ((@(Get-Content (Join-Path $runRoot 'captures/queue/results.jsonl'))).Count -eq 7 -and -not(Test-Path $replay)) 'consumed batch archived with refusal after runner restart'
Check (Test-Path (Join-Path $runRoot 'captures/queue/done/replay.json')) 'replay refusal retained job'
# Runtime game marker simulates a foreign game without querying/launching one.
$jobs[0].live_batch='fresh';$file=Join-Path $runRoot 'captures/queue/pending/foreign.json';$jobs[0] | ConvertTo-Json | Set-Content $file
$grant.jobs=@(@{hash=(Get-QueueJobHash $jobs[0] $runRoot);tree=$runRoot;build=$build;runs=1});$grant | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $runRoot 'captures/queue/authorizations/fresh.json')
Set-Content (Join-Path $runRoot 'fake_unowned_game') 'synthetic foreign process'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'scripts/test-queue.ps1') -DryRun -Root $runRoot -PermissionSession fake-runner
Check ((@(Get-Content (Join-Path $runRoot 'captures/queue/results.jsonl'))).Count -eq 7) 'unowned process blocks actual release path'
Check (-not(Test-Path (Join-Path $runRoot 'captures/queue/authorizations/fresh.json.state.json'))) 'unowned process did not consume scope or execute'
Write-Host "$count queue permission/fake-runner checks passed; scratch $root"
