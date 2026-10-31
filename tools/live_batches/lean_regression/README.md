# Lean regression batch (live_batch lean-regression), NOT authorized

Reusable regression after a consolidation or merge. One launch per save, foreground (the game takes focus and the pointer: the user must be away), FG 2, all A/B by toggling keys inside the run. Cost windows (`cost-*`, 25 s) have every diagnostic off; diagnostics (`diag-*`, about 80 ms/frame) run only in short dedicated windows; `shot-*` phases take one PNG trace each (no float bursts). lr2 and lr3 have `requires_success_of` lr1.
1. `lr1-camp-fg2` (Campsite - 27h, 156 s): warm-up, RT on a / `rt_cascade_casters` on / RT on b (the two RT-on windows are the casters OFF repeats), RT off, PNG pair RT off/on, 15 s primary match (rt_stats + rt_primary_check + epoch).
2. `lr2-inn-fg2` (The Blushing Mermaid - 96h 39m, 186 s): as lr1 (interior half of the casters gate), plus 15 s Q and 15 s E rotation with diagnostics on (rotation match with `rt_scene_wait=1`, see docs/INN_ROTATION_MATCH.md).
3. `lr3-box-s6-fg2` (Adamantine Forge, 161 s): 15 s `ls_check` (boxes off), 15 s `ls_box=1` with ls_stats, cost box off/on/off, PNG pair box off/on, 15 s primary match.

Pass: valid run (all expect lines, no forbid line); RR create Success; every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`; `TLAS ... none 0`; static primary match within the spread of the last validated run of the same save; cost-on-a and cost-on-b GPU ms and fps equal within their spread (that spread is the noise for every on/off delta); casters ON: `game lit & RT shadowed` not above both OFF windows; ls_check `differ in any bit on 0 px` (S7 failed this in cb4: report the count); box cost = local-pass GPU ms box 1 minus box 0. Report every delta with its GPU ms.

Duration: plans 503 s; with 3 launches about 14-15 min (at most 23 min of timeouts). Disk: 6 PNG traces at about 50-60 MB plus logs, about 0.4 GB.

## Start (only after the user's explicit go for this batch)
```powershell
$m='C:\Users\quali\Documents\Code Projects\BG3RTX'; $src="$m\.claude\worktrees\lean-jobs\tools\live_batches\lean_regression"
.\scripts\test-tree.ps1 -Status   # must be the main commit to test
New-Item -ItemType Directory -Force "$m\.claude\worktrees\test-main\captures\live_batches\lean_regression\plans" | Out-Null
Copy-Item "$src\plans\*.json" "$m\.claude\worktrees\test-main\captures\live_batches\lean_regression\plans"
$t=Get-Date; Get-ChildItem "$src\jobs\*.json" | Sort-Object Name | ForEach-Object { $d="$m\captures\queue\pending\$($_.Name)"; Copy-Item $_.FullName $d; (Get-Item $d).LastWriteTime=($t=$t.AddSeconds(1)) }
```
Before the copy the coordinator writes the receipt `captures\queue\authorizations\lean-regression.json` exactly as in ../consolidation/README.md (one entry per job, runs 1).

Results: `py -3 scripts/queue_results.py lr --lines`; PNG traces under test-main `captures/<stamp>/` (paths in the `[trace] frame ... written` lines). Rollback: delete the batch's files from pending, revoke the receipt, remove test-main captures/live_batches/lean_regression. No ini, save or default changes.

Offline check: `powershell -File tools\live_batches\lean_regression\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-10-31: all checks passed.
