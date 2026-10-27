# Offline test of test-queue.ps1's session mode: no game starts. Builds a scratch tree,
# queues jobs, runs test-queue.ps1 -DryRun (test-queue-fake.ps1 plays the game and the
# layer) and checks results.jsonl. Exit code 1 on a failed check.
#   .\scripts\test-queue-dryrun.ps1                 # scratch dir under %TEMP%\bg3rtx-queue-dryrun
param([string]$Out = (Join-Path $env:TEMP "bg3rtx-queue-dryrun\$(Get-Date -Format 'yyyyMMdd-HHmmss')"))
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force (Join-Path $Out "bin"), (Join-Path $Out "scripts"), (Join-Path $Out "captures\queue\pending") | Out-Null
$Out = (Resolve-Path $Out).Path
# A layer with session support carries the "scene ready" marker string (layer/tracer.cpp).
Set-Content (Join-Path $Out "bin\VkLayer_bg3rtx.dll") "fake layer: scene ready at present"
Set-Content (Join-Path $Out "bin\bg3rtx_core.dll") "fake core"
Copy-Item (Join-Path $PSScriptRoot "live_phases.py") (Join-Path $Out "scripts")
'{"start": "\\[trace\\] frame \\d+ written", "after_trace": [{"name": "reflections off", "set": {"rt_reflections": "0"}}]}' |
  Set-Content (Join-Path $Out "captures\ab.json")

# "post" runs through cmd /c (test-queue.ps1): touch with copy's "+,," idiom, not PowerShell.
$touchCore = 'copy /b bin\bg3rtx_core.dll +,, bin\bg3rtx_core.dll >nul'
$touchLayer = 'copy /b bin\VkLayer_bg3rtx.dll +,, bin\VkLayer_bg3rtx.dll >nul'
$guard = @('\[core\] guard:')
$jobs = @(
  @{ name = "a-gi0"; save = "Campsite"; session = $true; set = "gi=0"; traces = 1; warmup = 1; patterns = $guard; post = $touchCore },
  @{ name = "b-gi1-phases"; save = "Campsite"; session = $true; set = "gi=1"; traces = 2; warmup = 1; trace_gap = 2
     phases = "captures/ab.json"; patterns = $guard },
  @{ name = "c-refl0"; save = "Campsite"; session = $true; set = "rt_reflections=0"; traces = 1; warmup = 1; patterns = $guard },
  @{ name = "d-nv"; save = "Campsite"; session = $true; set = "fake_fault=nv"; traces = 1; warmup = 1; patterns = $guard },
  @{ name = "e-after-fault"; save = "Campsite"; session = $true; set = "gi=2"; traces = 0; warmup = 1; hold = 1; patterns = $guard },
  @{ name = "f-other-save"; save = "Rivington"; session = $true; set = "gi=3"; traces = 1; warmup = 1; patterns = $guard; post = $touchLayer },
  @{ name = "g-layer-rebuilt"; save = "Rivington"; session = $true; set = "gi=4"; traces = 1; warmup = 1; repeat = 2; patterns = $guard },
  @{ name = "h-single"; save = "Rivington"; set = "gi=5"; traces = 1; patterns = $guard },
  @{ name = "i-stall"; save = "Rivington"; session = $true; set = "fake_fault=stall"; traces = 1; warmup = 1 },
  @{ name = "j-after-stall"; save = "Rivington"; session = $true; set = "gi=6"; traces = 1; warmup = 1; patterns = $guard }
)
$t = (Get-Date).AddMinutes(-10)
foreach ($j in $jobs) {
  $f = Join-Path $Out "captures\queue\pending\$($j.name).json"
  $j | ConvertTo-Json | Set-Content $f
  (Get-Item $f).LastWriteTime = ($t = $t.AddSeconds(1))
}

$env:BG3RTX_FAKE_LOAD_SEC = "3"
$t0 = Get-Date
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "test-queue.ps1") -DryRun -Root $Out
Write-Host "queue took $([int]((Get-Date) - $t0).TotalSeconds)s; results in $Out\captures\queue"

$recs = @(Get-Content (Join-Path $Out "captures\queue\results.jsonl") | ConvertFrom-Json)
$by = @{}
foreach ($r in $recs) { $by["$($r.job)#$($r.run)"] = $r }
$fail = 0
function Check([string]$what, [bool]$ok) {
  Write-Host ("{0} {1}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $what)
  if (-not $ok) { $script:fail++ }
}
function Has($lines, [string]$re) { [bool](@($lines) -match $re) }
function Sess($k) { ($by[$k].session -split '#')[0] }
Check "10 jobs, 11 runs recorded" ($recs.Count -eq 11)
Check "a, b, c share one session" ((Sess "a-gi0#1") -and (Sess "a-gi0#1") -eq (Sess "b-gi1-phases#1") -and (Sess "b-gi1-phases#1") -eq (Sess "c-refl0#1"))
Check "a is run 1 of its session (nothing saved), c run 3 (saved > 0)" ($by["a-gi0#1"].saved_sec -eq 0 -and $by["c-refl0#1"].session -like "*#3" -and $by["c-refl0#1"].saved_sec -gt 0)
Check "a sees its own setting (gi=0)" (Has $by["a-gi0#1"].lines 'gi=0 ')
Check "b waited for the rebuilt core (generation 2)" ($by["b-gi1-phases#1"].core_gen -eq 2 -and $by["a-gi0#1"].core_gen -eq 1)
Check "b: 2 traces, phases applied after the first (rt_reflections=0)" (@($by["b-gi1-phases#1"].captures).Count -eq 2 -and (Has $by["b-gi1-phases#1"].lines 'gi=1 rt_reflections=0'))
Check "c: settings of b restored (gi=default, rt_reflections=0)" (Has $by["c-refl0#1"].lines 'gi=default rt_reflections=0 ')
Check "d: nv153 recorded as a fault, log slice kept" ($by["d-nv#1"].nv153 -ge 1 -and $by["d-nv#1"].outcome -match 'nv153' -and $by["d-nv#1"].log)
Check "e: relaunched after the fault" ((Sess "e-after-fault#1") -ne (Sess "d-nv#1") -and $by["e-after-fault#1"].session -like "*#1")
Check "e: no trace, hold, sees gi=2" (@($by["e-after-fault#1"].captures).Count -eq 0 -and (Has $by["e-after-fault#1"].lines 'gi=2 '))
Check "f: other save, new session" ((Sess "f-other-save#1") -ne (Sess "e-after-fault#1"))
Check "g: layer DLL rebuilt -> new session, then reused for run 2" ((Sess "g-layer-rebuilt#1") -ne (Sess "f-other-save#1") -and (Sess "g-layer-rebuilt#2") -eq (Sess "g-layer-rebuilt#1"))
Check "h: single run, no session" (-not $by["h-single#1"].session -and $by["h-single#1"].outcome -match 'Run ended: done' -and @($by["h-single#1"].captures).Count -eq 1)
Check "i: stall ends the session" ($by["i-stall#1"].outcome -match 'stall')
Check "j: relaunched after the stall, sees gi=6" ((Sess "j-after-stall#1") -ne (Sess "i-stall#1") -and (Has $by["j-after-stall#1"].lines 'gi=6 '))
Check "every session run reports done unless faulted" (-not ($recs | Where-Object { $_.job -notmatch '^(d-nv|i-stall)$' -and $_.outcome -notmatch 'Run ended: done' }))
Check "no session left running (no session.ready / trace.request)" (-not (Test-Path (Join-Path $Out "captures\session.ready")) -and -not (Test-Path (Join-Path $Out "trace.request")))
Get-Content (Join-Path $Out "captures\queue\results.md")
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host "all checks passed"
