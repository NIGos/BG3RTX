# D-EM Adamantine Forge batch (live_batch em-forge), NOT authorized

One launch, foreground (the game takes focus and the pointer: the user must be away), FG 2, every A/B by toggling keys inside the run. Question: why the exact family `304be1c7` Base_GM_Pulse_FX reads ours/game 0.36 on its bright pixels in AF (band 0.842-1.187) while GF passes at 0.999 (docs/TRACK_D_EMISSIVE.md 13.22). The build to test must contain the em-forge exclusion (`rt_em_fx_pulse`, default 0).

1. `ef1-af-em-fg2` (Adamantine Forge, 126 s of plan): `rt_emissive=1` at launch (startup-only), `rt_em_dbg_fam` = `304be1c7`, `rt_timing_frames=120` (one `[em] projected check` window per 120 frames). Exposure held from the first in-scene phase.
   - `warmup` 20 s, `gate-excl-a` 20 s: `rt_em_fx_pulse=0`, the family excluded.
   - `gate-fx-a`, `gate-fx-b` 25 s each: `rt_em_fx_pulse=1`, the family captured. Its ratio per window and its record dump (radiance, mode, CB words 0-7, time, pulse, hits with ours = 0, ours without the maps).
   - `diag-view-19`, `diag-view-20` 8 s each: one PNG trace each of the family alone, ours (19) and the game's effects target (20).
   - `gate-excl-b` 15 s: `rt_em_fx_pulse=0` again; `restore` 5 s.

What each line answers:
- Exclusion works: the `[em] soup ...` line shows `draws of excluded families (rt_em_fx_pulse=0: 304be1c7)` above 0 in the excl windows and 0.00 in the fx windows. `[em] emissive families` shows the family's draws, with 0 captured while it is excluded. The other families' ratios in the excl windows equal those in the fx windows within their window spread (s_w).
- The family's gate: `py -3 -m scripts.qa.em_gate <kept log>`. `304be1c7` appears only in the fx windows (about 20 windows). The other families use every window.
- Level or pattern: in the record dump, `per hit ours`, `ours without the maps` and `game`. If game / ours is the same in every window and the PNG pair shows the same pattern at different brightness, a factor is missing: compare the CB words with the fragment shader (captures/alpha_cache/304be1c71df194cb.glsl). If the PNG pair shows different patterns (bright texels where the game is dark), the mask lookup or the geometry is wrong, not a constant.
- Cost: `[em] per build` build median and the effects-pass ms with capture, excl windows against fx windows.

Pass (to lift the exclusion): `304be1c7` passes em_gate in AF, the other gated families still pass, and GF still passes (a GF run of the same job with the save changed). Until then `rt_em_fx_pulse` stays 0.

Duration: plan 126 s, with the load about 4-5 min (at most 7 min of timeout). Disk: 2 PNG traces at about 50-60 MB plus the log, about 0.15 GB.

## Start (only after the user's explicit go for this batch)
```powershell
$m='C:\Users\quali\Documents\Code Projects\BG3RTX'; $src="$m\.claude\worktrees\em-forge\tools\live_batches\em_forge"
.\scripts\test-tree.ps1 -Status   # must be a build that contains rt_em_fx_pulse
New-Item -ItemType Directory -Force "$m\.claude\worktrees\test-main\captures\live_batches\em_forge\plans" | Out-Null
Copy-Item "$src\plans\*.json" "$m\.claude\worktrees\test-main\captures\live_batches\em_forge\plans"
$t=Get-Date; Get-ChildItem "$src\jobs\*.json" | Sort-Object Name | ForEach-Object { $d="$m\captures\queue\pending\$($_.Name)"; Copy-Item $_.FullName $d; (Get-Item $d).LastWriteTime=($t=$t.AddSeconds(1)) }
```
Before the copy the coordinator writes the receipt `captures\queue\authorizations\em-forge.json` exactly as in ../consolidation/README.md (one entry, runs 1).

Results: `py -3 scripts/queue_results.py ef --lines`; then em_gate on the kept log; PNG traces under test-main `captures/<stamp>/` (paths in the `[trace] frame ... written` lines). Rollback: delete the job from pending, revoke the receipt, remove test-main captures/live_batches/em_forge. No ini, save or default changes.

Offline check: `powershell -File tools\live_batches\em_forge\dryrun.ps1 [-Out <scratch dir>]`.
