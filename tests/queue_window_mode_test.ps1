$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'scripts\queue-window-mode.ps1')
. (Join-Path $repo 'scripts\queue-permission.ps1')
Add-Type -Path (Join-Path $repo 'scripts\visible-unfocused.cs')
$count=0
function Check([bool]$value,[string]$label) { if(-not $value){throw "FAIL $label"}; $script:count++ }
function Reject($job) { try { $null=ConvertTo-QueueWindowJob $job; return $false } catch { return $true } }
$ordinary=[pscustomobject]@{args=@('-Foreground');set='fg=2';session=$true}
Check ([object]::ReferenceEquals($ordinary,(ConvertTo-QueueWindowJob $ordinary))) 'default job unchanged'
$job=[pscustomobject]@{window_mode='visible_unfocused';args=@('-Foreground','-Move');set='fg=4,headless=1';session=$true}
$normalized=ConvertTo-QueueWindowJob $job
Check ($normalized.args -contains '-NoActivate' -and $normalized.args -contains '-RealPresent' -and $normalized.args -notcontains '-Foreground') 'unfocused real-present arguments'
Check ($normalized.args -contains '-Move' -and -not $normalized.session) 'posted input retained, session reuse disabled'
Check ($normalized.set -match ',fg=0,headless=0$') 'forced FG off and real presentation'
Check ($job.args -contains '-Foreground' -and $job.session -and $job.set -eq 'fg=4,headless=1') 'source job not mutated'
Check ((ConvertTo-QueueWindowJob ([pscustomobject]@{args=@('-NoActivate')})).window_mode -eq 'visible_unfocused') 'direct switch normalized'
Check (Reject ([pscustomobject]@{window_mode='other'})) 'unknown mode rejected'
Check (Reject ([pscustomobject]@{window_mode='visible_unfocused';command='custom runner'})) 'custom launch cannot bypass mode'
Check (Reject ([pscustomobject]@{window_mode='visible_unfocused';phases='uncontrolled.ini'})) 'uncontrolled live phases rejected'
$lines=Set-UnfocusedIniLines @('fg=4','headless=1','  fg = 2','gi=1','FG=3')
Check (@($lines | Where-Object {$_ -match '^fg='}).Count -eq 1 -and $lines -contains 'fg=0') 'phase cannot re-enable FG'
Check ($lines -contains 'headless=0' -and $lines -contains 'gi=1') 'phase preserves lighting and real presentation'
$scratch=Join-Path $env:TEMP ('bg3-unfocused-policy-'+[guid]::NewGuid().ToString('N'))
$q=Join-Path $scratch 'captures\queue'; New-Item -ItemType Directory -Force $q | Out-Null
Check ((Get-QueuePermissionBlock $job $scratch) -match 'USER_AWAY') 'new mode retains away requirement'
Set-Content -LiteralPath (Join-Path $q 'USER_AWAY') 'synthetic policy test only'
Check (-not (Get-QueuePermissionBlock $normalized $scratch)) 'away permits owned mode'
Set-Content -LiteralPath (Join-Path $q 'USER_PLAYING') 'synthetic policy test only'
Check ((Get-QueuePermissionBlock $normalized $scratch) -match 'USER_PLAYING') 'playing blocks unfocused too'
$r=[BG3RTX.UnfocusedWindow+Rect]::new(0,0,100,100)
$none=[BG3RTX.UnfocusedWindow+Rect[]]@()
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($r,$none) -eq 10000) 'uncovered area'
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($r,@($r)) -eq 0) 'full occlusion fails'
$half=[BG3RTX.UnfocusedWindow+Rect]::new(0,0,50,100)
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($r,@($half,$half)) -eq 5000) 'overlapping covers do not double subtract'
$strip=[BG3RTX.UnfocusedWindow+Rect]::new(0,0,99,100)
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($r,@($strip)) -eq 100) 'visible one-pixel strip retained'
$otherHalf=[BG3RTX.UnfocusedWindow+Rect]::new(50,0,100,100)
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($r,@($half,$otherHalf)) -eq 0) 'combined windows fully cover'
$empty=[BG3RTX.UnfocusedWindow+Rect]::new(100,0,50,100)
Check ([BG3RTX.UnfocusedWindow]::RemainingArea($empty,$none) -eq 0) 'off-monitor empty clip'
Check ([BG3RTX.UnfocusedWindow]::ShowNoActivate -eq 4 -and ([BG3RTX.UnfocusedWindow]::NoActivatePositionFlags -band 0x10)) 'non-activating API constants'
Check (-not [BG3RTX.UnfocusedWindow].GetMethod('SetForegroundWindow')) 'no focus-setting native method'
$fgJob=ConvertTo-QueueWindowJob ([pscustomobject]@{window_mode='visible_unfocused_fg';args=@('-Foreground');set='fg=2'})
Check ($fgJob.args -contains '-UnfocusedFG' -and $fgJob.set -notmatch 'fg=0') 'authorized FG mode preserves FG on'
Check ((Set-UnfocusedIniLines @('fg=2','headless=1') -PreserveFG) -contains 'fg=2') 'live FG phases preserved'
$otherScratch=Join-Path $env:TEMP ('bg3-fg-authorized-'+[guid]::NewGuid().ToString('N'))
Check (-not (Get-QueuePermissionBlock $fgJob $otherScratch)) 'explicit authorization no longer needs USER_AWAY'
Check ((Get-QueuePermissionBlock $fgJob $scratch) -match 'USER_PLAYING') 'playing still blocks authorized FG mode'
Write-Host "$count unfocused policy/geometry checks passed; no process or window methods invoked"
