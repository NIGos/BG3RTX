# Synthetic source-freeze test only; never a real inbox or game binary.
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/queue-permission.ps1')
$root=Join-Path $env:TEMP ('bg3-queue-source-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $root 'bin'),(Join-Path $root 'scripts') | Out-Null
Set-Content (Join-Path $root 'bin/VkLayer_bg3rtx.dll') 'fake layer'
Set-Content (Join-Path $root 'bin/bg3rtx_core.dll') 'fake core'
$helper=Join-Path $root 'scripts/visible-unfocused.cs'
Set-Content $helper 'public class Fake { public const int Value = 1; }'
$before=Get-QueueBuildIdentity $root
Set-Content $helper 'public class Fake { public const int Value = 2; }'
$after=Get-QueueBuildIdentity $root
if($before -ceq $after){throw 'Runtime C# helper mutation did not invalidate candidate identity'}
Set-Content $helper 'public class Fake { public const int Value = 1; }'
if($before -cne (Get-QueueBuildIdentity $root)){throw 'Restored source did not restore identity'}
Write-Host '2 runtime C# source-freeze checks passed; scratch' $root
