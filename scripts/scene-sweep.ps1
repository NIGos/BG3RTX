# Runs auto-run.ps1 once per test scene (docs/TEST_SCENES.md) and writes a
# summary of which scenes loaded and where their captures are.
#   .\scripts\scene-sweep.ps1                      all scenes
#   .\scripts\scene-sweep.ps1 -Only S2,S8 -Set rt_reflections=0
param(
  [string[]]$Only = @(),
  [string[]]$Set = @(),
  [int]$TimeoutSec = 240,
  [switch]$Move
)
# Continue: auto-run.ps1 writes native stderr (build, game) that must not abort the sweep.
$ErrorActionPreference = "Continue"
# -File passes "S2,S8" as one string: split it.
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Set = @($Set | ForEach-Object { $_ -split "," } | Where-Object { $_ })
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") 2>&1 | Select-Object -Last 1
$root = Split-Path $PSScriptRoot
$scenes = [ordered]@{
  S1 = "QuickSave_3784"; S2 = "Rivington - 74h"; S3 = "Campsite - 27h"; S4 = "Goblin Camp - 18h";
  S5 = "Grymforge - 32h 59m"; S6 = "Adamantine Forge"; S7 = "The Blushing Mermaid - 96h"; S8 = "Water Queens House";
  S9 = "che Yllek"; S10 = "Shadow-Cursed Lands - 52h"; S11 = "Gauntlet of Shar - 57h";
  S12 = "Moonrise Towers Main Floor - 65h"; S13 = "Mind Flayer Colony"; S14 = "Lower City - 91h"
}
$summary = @()
$first = $true
foreach ($id in $scenes.Keys) {
  if ($Only.Count -and $Only -notcontains $id) { continue }
  # A child process per run: its stderr is plain text here, never a terminating error.
  $runArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "auto-run.ps1"),
               "-NoBuild", "-Background", "-Save", $scenes[$id], "-TimeoutSec", $TimeoutSec)
  if ($Move) { $runArgs += "-Move" }
  if ($Set.Count) { $runArgs += @("-Set", ($Set -join ",")) }
  $first = $false
  Write-Host "==== $id $($scenes[$id])"
  $before = Get-Date
  $out = & powershell @runArgs 2>&1 | Out-String
  $cap = Get-ChildItem (Join-Path $root "captures") -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}$' -and $_.LastWriteTime -ge $before } |
         Sort-Object Name | Select-Object -Last 1
  $ok = $out -match "Run ended: done"
  $summary += [pscustomobject]@{ Id = $id; Save = $scenes[$id]; Loaded = $ok; Capture = $(if ($ok -and $cap) { $cap.Name } else { "" }) }
  Write-Host "$id loaded=$ok capture=$($cap.Name)"
}
$summary | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$summary | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $root "captures\scene-sweep.json")
