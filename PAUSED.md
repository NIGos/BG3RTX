# PAUSED: grass alpha fix (Track D, rt_common.hlsli / alpha table)

Branch `worktree-agent-ac91da91fddcd3510`, main merged in (617d8df). Not merged into main.

## Done and verified
- Root cause: the grass/ground-cover Granite shaders (`73849471` + 23 others) read
  the sample's `.w` through a vec4 temporary; `scripts/alpha_table.py` missed them,
  so they were opaque in the TLAS. Flat ground cards blocked every GI ray (black
  polygons), reflections, and the sun (card-shaped shadows). Evidence: `rt_debug=5`
  showed "in front, opaque" (purple) on the patches; `rt_debug=4` visibility 0;
  the alpha counters showed 0% fallbacks in steady state.
- Fixed in `alpha_table.py` (+ regenerated `shaders/rt/alpha_fs.txt`). Also the
  effective cut-off (76 shaders used a 0.001/0.01 early-out as their cut-off).
- `rt_common.hlsli`: per-class alpha counters (`rt_stats`), resident level of the
  read, stochastic threshold at blurred reads, unknown cutouts clear for visibility
  rays only, bounds checks. Live switches `rt_alpha_cut`, `rt_alpha_stochastic`,
  `rt_alpha_unknown` (`bg3rtx.ini`, header uint 2 written by `layer/alphatest.cpp`).
- Campsite A/B images: patches gone, twig/grass sun shadows have blade shapes
  (`docs/img/alpha_campsite_*`). 0 non-finite.
- QA harness S3: hard-dark GI 10.9 -> 0, all RT 20.4 -> 0 (see TRACK_D_RT 7.1b table).
- `-Validation` in Campsite gameplay (412 alpha draws): same message IDs as the
  HEAD baseline (`captures/val_base_ids.txt`).
- Docs: TRACK_D_RT 7.1b, QUALITY Q16 + status row.

## In progress / not verified
- `-Move` run (Goblin Camp): killed at 70 s by another session's
  `captures\run_all.ps1`, which launches the game without the mutex (it also killed
  3 earlier runs). No fault seen before that.
- QA brightening rose (GI 2.3 -> 7.9‰, sun 0.5 -> 4.4‰): light now passes through
  grass where SSAO / the game's shadow map treat it as solid. Not yet separated
  from the stochastic part.
- Foundry chains/railings A/B not re-run after the table change (S1 QA ran: hard-dark GI 0.066‰, no baseline).

## Next steps (only when no other session is running the game)
1. `.\scripts\auto-run.ps1 -NoBuild -Background -Move -MoveSec 20 -Save "Goblin Camp - 18h" -Traces 3 -TimeoutSec 1200 -Set "auto_interval=600,dump_png_width=2560,rt_stats=1"`; check no faults, foliage OK.
2. Set `rt_alpha_stochastic=0` in bg3rtx.ini (temporarily), `py -3 -m scripts.qa run --scenes S3 --out captures\qa\fixed_hardcut`; compare brightening/flicker with `captures/qa/20261026-192425` (fixed) and `captures/qa/baseline_legacy`; restore the ini.
3. Foundry (S1) A/B of chains/railings: `rt_alpha_cut` toggle via `scripts/live_phases.py`.
4. Update TRACK_D_RT 7.1b with the results; report.
