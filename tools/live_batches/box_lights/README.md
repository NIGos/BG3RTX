# Box lights `ls_box=2` batch (live_batch box-lights), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save (the game takes focus and the pointer: the user must be away; with the user at the PC use `"window_mode": "visible_unfocused_fg"` and judge only GPU ms and energy), FG 2, keys toggled inside the run, diag-off `cost-*` windows, one PNG per 8 s `shot-*` phase, no float bursts. Receipt `box-lights.json`; the plans go to test-main `captures\live_batches\box_lights\plans`. Design: [TRACK_D_LOCAL_LIGHTS](../../../docs/TRACK_D_LOCAL_LIGHTS.md) 9.10 "`ls_box=2`".

1. `bx1-box-s6-fg2` (Adamantine Forge, S6; 160 s of plan): box lights 37.9% of the lit luminance, 0.009/px traced with `ls_box=1`.
2. `bx2-box-s7-fg2` (The Blushing Mermaid - 96h 39m, S7 inn; 160 s; runs only if job 1 is valid): character rim/fill rigs, cookies, no shadow-mapped box in view.

Phases (both): `warmup-off` 20 s; `diag-box1-stats` 15 s (`ls_box=1`, `ls_stats=1`); `settle-box2` 5 s (stats off: the next window starts clean); `diag-box2-stats` 15 s (`ls_box=2`, `ls_stats=1`); `cost-off-a` / `cost-on` (`ls_box=2`) / `cost-off-b` 20 s each; `shot-off-a`, `shot-on-a`, `shot-off-b`, `shot-on-b` 8 s each (one PNG); `view-removed` 8 s (`ls_debug=2`, light removed x4, one PNG); `restore`. `ls_shadowed=3`, `ls_k=0`, `ls_samples=0` throughout.

## Gates (read the deciding numbers in the log)
- Valid run: all expect lines, no forbid line, RR create Success, DLSS-G `2.00`, `TLAS ... none 0`, `ls box: active (mode 1;` and `(mode 2;`, an `ls: ... box 2)` and a `box 0)` GPU line.
- Shadow-mapped segment (S6): `ls box trace (120 frames): mode 1;` vs `mode 2;`: `occluded where the game's atlas term is 1` must fall from about all of the shadow-mapped occluded rays (lr3: 847,547 of 847,557 occluded) to a minority, with `segment cut at the shadow view's near plane` above 0. If it does not fall, the near-plane cut is wrong (pancaking: casters before the plane do cast): drop that half of mode 2.
- Unshadowed boxes: `mode 2; ... unshadowed traced` > 0, `occluded` share and `removed unshadowed` (% of the lit luminance) reported. `receiver in the light-side fade-in` is the share the fixture rule leaves at the game's look.
- Accuracy (`ls box bound`, both modes): `recompute at or above the game's stored value's upper neighbour` on 0 px is the pass (threshold: the B10G11R11 encoding, no allowance). A nonzero count is a box-recompute over-estimate (or fog transmittance: then it is also nonzero in the mode-1 window at the same px count); `inconclusive` must be 0 (else the native format is not B10G11R11 there). `recompute ... % of game` is the share of the stored light our recompute explains (the rest is sun, ambient, in-scatter).
- Energy (visibility only, can only darken): the float mean luminance in `dumps.json` of the scene colour before post (fs `5c3fdabc477f316a`, `0.2126 r + 0.7152 g + 0.0722 b` of its `mean`), each ON shot against the mean of its neighbouring OFF shots. Pass: ON <= OFF + the OFF-a/OFF-b spread, and OFF - ON within the spread of the predicted darkening (`removed unshadowed` + `removed shadow-mapped` of the mode-2 window, times the lit share of the scene colour). Brighter than OFF beyond the spread fails. Never the PNG previews or bloom dumps.
- Cost: `ls: local-light shadow pass GPU median ... box 2)` of `cost-on` against the mean of `cost-off-a` and `cost-off-b` (`box 0)`), with the OFF spread; expected +0.15 to +0.3 ms. Also `rt passes: GPU cost`.
- Look (PNG pairs, `view-removed`): light no longer leaks through walls or floors; no new `hard_dark` region away from real casters (QA detectors as in lean_regression). The main-menu skull beam is a separate user check (the menu is not in these saves).

Duration: 2 x 160 s of plan plus 2 loads, about 9-10 min (each job at most 7.7 min of timeout). Disk: 10 PNG traces at about 60 MB plus logs, about 0.65 GB.

## Start (only after the user's explicit go for this batch)
As in ../lean_regression/README.md with this folder, prefix `bx` and receipt `box-lights.json` (test-main must be the commit under test: a core and shader change, no layer restart beyond the launch). Results: `py -3 scripts/queue_results.py bx --lines`. Rollback: delete the jobs from pending, revoke the receipt, remove test-main captures/live_batches/box_lights. No ini, save or default changes.

Offline check: `powershell -File tools\live_batches\box_lights\dryrun.ps1 -Out <fresh scratch dir>` (a reused dir keeps old results.jsonl lines and fails the run A/B counts). Last result 2026-11-01: all checks pass.
