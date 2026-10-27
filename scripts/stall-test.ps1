# Repeats short Campsite runs (load + 2 traces) per setting variant and counts
# how many stall (auto-run "Run ended: stalled"). For bisecting the intermittent
# render stall (docs/STATUS.md).
#   .\scripts\stall-test.ps1 -Variants "rr_post=0","rr_post=1" -Runs 3
param([string[]]$Variants = @("rr_post=0", "rr_post=1"), [int]$Runs = 3, [string]$Save = "Campsite - 27h")
$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot
$out = Join-Path $root "captures\stall-test.log"
foreach ($r in 1..$Runs) {
  foreach ($v in $Variants) {
    $t0 = Get-Date
    $o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "auto-run.ps1") -NoBuild -Background -NoAnalyze `
      -Save $Save -Traces 2 -TimeoutSec 300 -Set "$v,auto_interval=900" 2>&1 | Out-String
    $res = if ($o -match "Run ended: (\w[\w ]*) after (\d+)s") { "$($Matches[1]) $($Matches[2])s" } else { "?" }
    $line = "$(Get-Date -Format s) run $r $v -> $res"
    Add-Content $out $line
    Write-Host $line
  }
}
