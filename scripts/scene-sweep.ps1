# Runs auto-run.ps1 once per test scene (docs/TEST_SCENES.md) and writes a
# summary of which scenes loaded and where their captures are.
#   .\scripts\scene-sweep.ps1                      all scenes, two at a time
#   .\scripts\scene-sweep.ps1 -Only S2,S8 -Set rt_reflections=0
#   .\scripts\scene-sweep.ps1 -Exclusive           one at a time, alone on the GPU (timings)
# Two lanes (auto-run.ps1 -Tag a / -Tag b) run shared background games side by side;
# each lane's output is in captures\sweep_<id>.out.
param(
  [string[]]$Only = @(),
  [string[]]$Set = @(),
  [int]$TimeoutSec = 240,
  [switch]$Move,
  [ValidateRange(1, 2)]
  [int]$Parallel = 2,
  [switch]$Exclusive
)
# Continue: auto-run.ps1 writes native stderr (build, game) that must not abort the sweep.
$ErrorActionPreference = "Continue"
# -File passes "S2,S8" as one string: split it.
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Set = @($Set | ForEach-Object { $_ -split "," } | Where-Object { $_ })
if ($Exclusive) { $Parallel = 1 }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") 2>&1 | Select-Object -Last 1
$root = Split-Path $PSScriptRoot
$caps = Join-Path $root "captures"
$scenes = [ordered]@{
  S1 = "QuickSave_3784"; S2 = "Rivington - 74h"; S3 = "Campsite - 27h"; S4 = "Goblin Camp - 18h";
  S5 = "Grymforge - 32h 59m"; S6 = "Adamantine Forge"; S7 = "The Blushing Mermaid - 96h"; S8 = "Water Queens House";
  S9 = "che Yllek"; S10 = "Shadow-Cursed Lands - 52h"; S11 = "Gauntlet of Shar - 57h";
  S12 = "Moonrise Towers Main Floor - 65h"; S13 = "Mind Flayer Colony"; S14 = "Lower City - 91h"
}
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($id in $scenes.Keys) { if (-not $Only.Count -or $Only -contains $id) { $queue.Enqueue($id) } }
$lanes = @("a", "b")[0..($Parallel - 1)]
$running = @{}   # lane -> @{ Id; Proc; Before; Out }
$summary = @()
$sweepStart = Get-Date

function Finish-Run($lane, $r) {
  $out = (Get-Content $r.Out -Raw -ErrorAction SilentlyContinue) + (Get-Content "$($r.Out).err" -Raw -ErrorAction SilentlyContinue)
  $cap = Get-ChildItem $caps -Directory | Where-Object { $_.Name -match "^\d{8}-\d{6}-$lane$" -and $_.LastWriteTime -ge $r.Before } |
         Sort-Object Name | Select-Object -Last 1
  $ok = $out -match "Run ended: done"
  $mode = if ($out -match "Run mode: (\w+)") { $Matches[1] } else { "?" }
  $sec = [int]((Get-Date) - $r.Before).TotalSeconds
  Write-Host "$(Get-Date -f T) $($r.Id) loaded=$ok capture=$($cap.Name) ${sec}s ($mode)"
  [pscustomobject]@{ Id = $r.Id; Save = $scenes[$r.Id]; Loaded = $ok; Capture = $(if ($ok -and $cap) { $cap.Name } else { "" })
                     Seconds = $sec; Mode = $mode }
}

while ($queue.Count -or $running.Count) {
  foreach ($lane in $lanes) {
    if ($running.ContainsKey($lane) -or -not $queue.Count) { continue }
    $id = $queue.Dequeue()
    # A child process per run: its stderr is plain text here, never a terminating error.
    $runArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$(Join-Path $PSScriptRoot 'auto-run.ps1')`"",
                 "-NoBuild", "-Background", "-Save", "`"$($scenes[$id])`"", "-TimeoutSec", $TimeoutSec, "-Tag", $lane)
    if ($Move) { $runArgs += "-Move" }
    if ($Exclusive) { $runArgs += "-Exclusive" }
    if ($Set.Count) { $runArgs += @("-Set", ($Set -join ",")) }
    $out = Join-Path $caps "sweep_$id.out"
    Write-Host "$(Get-Date -f T) ==== $id $($scenes[$id]) (lane $lane)"
    $before = Get-Date
    $p = Start-Process powershell -ArgumentList $runArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError "$out.err"
    $running[$lane] = @{ Id = $id; Proc = $p; Before = $before; Out = $out }
    Start-Sleep -Seconds 3   # lanes start a little apart (lock hand-over, log files)
  }
  Start-Sleep -Seconds 2
  foreach ($lane in @($running.Keys)) {
    $r = $running[$lane]
    if ($r.Proc.HasExited) { $summary += Finish-Run $lane $r; $running.Remove($lane) }
  }
}
$total = [int]((Get-Date) - $sweepStart).TotalSeconds
$summary = @($summary | Sort-Object { [int]($_.Id.Substring(1)) })
$summary | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "Sweep: $($summary.Count) scenes in ${total}s, $Parallel at a time$(if ($Parallel -gt 1) { ' (shared runs: per-scene seconds are not exclusive timings)' })"
$summary | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $caps "scene-sweep.json")
