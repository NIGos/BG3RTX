# Consolidation live batch (live_batch consolidation-20261031), NOT authorized yet

Six single runs on the test tree (main build, ABI 23), visible window behind others, FG 2 (`visible_unfocused_fg`), same-run A/B via motion-plan phases. Jobs 3-7 have `requires_success_of` job 1: if job 1 is INVALID they stay in pending and claim nothing.
1. `cb1-regression-camp-fg2` (Campsite - 27h): RT all-off/on twice (the Ctrl+Shift+F9 key set; the hotkey itself reads the physical keyboard), then `rt_cascade_casters` 0/1/0 (outdoor half of its gate, plus the runtime compile of the `RT_CASCADE_MASKS` define variant). Pass: valid run, no forbid line (crash, device lost, core rejected, shader FAILED, RR failure), RR create Success, every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`, `TLAS ... none 0` in RT-on windows, RT-on-a and on-b primary match equal within the spread of their own 120-frame windows. File-save shader hot reload cannot be triggered: touching a shader changes the frozen build, which makes the run INVALID.
2. `cb2-scene-wait-rotation-inn-fg2`: retired 2026-11-01 (job and plan deleted). It tested `rt_scene_wait=2`, removed on 2026-10-31 (TRACK_D_SCENE_BUILD.md), and its post step ran the rebase-era `scripts.qa.rebase_gate`. Its last result: docs/INN_ROTATION_MATCH.md.
3. `cb3-cascade-roofs-inn-fg2`: TRACK_D 8.5 "Gate (first plan)", interior half. OFF/ON/OFF, 90 s each, a K16 burst each. Pass: `[cascade] matrix` orthographic with origin about 0 m; builds with the instance about 100%; key on `kept by roofs` 0% and brightened px not above key-off; `game lit & RT shadowed` not up versus both OFF windows (here and in job 1 outdoors); mean brightening equal within the OFF-repeat spread. Cost: the casters pass ms and the `[rt] scene build GPU` lines.
4-6. `cb4-box-s7`, `cb5-box-s6`, `cb6-box-s5`: P3_BOX_LIGHTS "Deciding queue work" 1 and 3. `ls_check` reference with boxes off (forbid: any nonzero any-bit difference), OFF/ON/OFF with ls_stats, then ON/OFF without stats for cost. Pass: `ls box: active`, traced > 0, exact unknown/cookie integers reported, local-pass GPU ms box 1 versus box 0. S7 has K16 bursts: the ON local-channel mean drop should match the reported "removed" share; no temporal events above the OFF repeat. Still open after this batch: debug4 recompute accuracy and the main-menu skull/red gem rule.
7. `cb7-dialogue-flicker-inn-fg2`: DIALOGUE_SHADOWS "same-pose diagnostics" in the user's trial config. One K16 burst each: native (RT off, calibration), user config x3, RT sun off, local off, boxes off, casters/current sun off, native repeat. A channel flickers from added RT if its transient events exceed the native bursts' bound and vanish when that effect is off. Limit: the queue cannot open a dialogue (no click step); this is the inn start view only.

Duration: motion plans 2475 s; with 6 launches about 55 min, at most 71 min (sum of timeouts). Bursts 15 x about 2.3 GB.

## Start (only after the user's explicit go)
```powershell
$m='C:\Users\quali\Documents\Code Projects\BG3RTX'; $src="$m\.claude\worktrees\live-batch\tools\live_batches\consolidation"
.\scripts\test-tree.ps1 -Status   # must be the main commit to test
New-Item -ItemType Directory -Force "$m\.claude\worktrees\test-main\captures\live_batches\consolidation\plans" | Out-Null
Copy-Item "$src\plans\*.json" "$m\.claude\worktrees\test-main\captures\live_batches\consolidation\plans"
$t=Get-Date; Get-ChildItem "$src\jobs\*.json" | Sort-Object Name | ForEach-Object { $d="$m\captures\queue\pending\$($_.Name)"; Copy-Item $_.FullName $d; (Get-Item $d).LastWriteTime=($t=$t.AddSeconds(1)) }
```
Before the copy, the coordinator writes `captures\queue\authorizations\consolidation-20261031.json` recording the user's go, with one entry per job: hash `Get-QueueJobHash (ConvertTo-QueueWindowJob $job) $tree`, tree test-main, build `Get-QueueBuildIdentity $tree`, runs 1 (as in scripts/test-queue-dryrun.ps1). Then start the runner with `-PermissionSession` equal to the receipt's runner_session.

## Results
`py -3 scripts/queue_results.py cb --lines` (or `cb1`, `cb3`, ...). Kept logs and runner logs: captures/queue/logs/. Bursts afterwards, offline: `py -3 -m scripts.qa.pan_artifacts --log <kept log> --out <dir>` per job (first static burst calibrates).

## Rollback
Delete the batch's files from captures/queue/pending (jobs blocked after a job-1 failure stay there), set the receipt status to revoked, remove test-main captures/live_batches/consolidation. Every key is set per run; no ini, save or default is changed.

Offline check: `powershell -File tools\live_batches\consolidation\dryrun.ps1` (static checks, stop-on-failure run, structural run).
