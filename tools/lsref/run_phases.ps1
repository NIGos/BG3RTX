param([string]$Phases, [string]$Tag, [string]$Save = "", [switch]$Validation, [switch]$Move)
$root = Split-Path $PSScriptRoot
Set-Location $root
$p = Start-Process -PassThru -NoNewWindow -FilePath py -ArgumentList "-3", "scripts\live_phases.py", $Phases -RedirectStandardOutput "captures\phases_$Tag.txt"
$job = Start-Job -ScriptBlock { param($id, $done) Wait-Process -Id $id; New-Item -ItemType File $done -Force | Out-Null } -ArgumentList $p.Id, (Join-Path $root "captures\auto.done.pending")
# end the run as soon as the phases are done
$w = Start-Job -ScriptBlock { param($root)
  while (-not (Test-Path "$root\captures\auto.done.pending")) { Start-Sleep 2 }
  Start-Sleep 5; Remove-Item "$root\captures\auto.done.pending"; New-Item -ItemType File "$root\captures\auto.done" -Force | Out-Null } -ArgumentList $root
& .\scripts\auto-run.ps1 -Background -NoBuild -Traces 2 -TimeoutSec 1500 -Save $Save -Validation:$Validation -Move:$Move -Set "dump_images=0,auto_interval=1000000" *> "captures\run_$Tag.txt"
Stop-Job $w -ErrorAction SilentlyContinue; Remove-Job $w, $job -Force -ErrorAction SilentlyContinue
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Remove-Item "captures\auto.done.pending" -ErrorAction SilentlyContinue
Get-Content "captures\run_$Tag.txt" -Tail 1
