## rf_hit_gi: the screen's indirect diffuse and ls_box=2 box visibility at reflection hits (branch worktree-agent-a05080ba75170ea35, READY_FOR_LIVE, 2026-11-01)

- Why: tier (a) bias S8 0.925 (diffuse 0.101 vs the screen's 0.115: the screen adds the GI bounce, the hit had
  ambient x AO only), S6 1.089, camp 1.097. User decision: rf_hit_shade stays experimental until the bias is within
  the game's spread; the threshold is not lowered.
- Key `rf_hit_gi=0` (live, needs rf_hit_shade=1; GUI Experimental "Indirect light in hidden-surface reflections").
  Variant define `RF_HIT_GI=1` on the four hit-shade files (own Pipes, 192 new requests). Key off: every existing
  request byte-identical (rf_hit_shade_shader_test).
  - 1: the hit's `hit_ao_rays` rays (same rays and A as before) also take the GI trace's bounce rule (screen hit:
    prev_bounce_at x w; SHaRC hit found: cache x w). Term = bounce x diffuse colour x gi_strength (0 without the
    GI composite), composed as the screen (`common/hit_gi_compose.h`).
  - 2: as 1, but the tier (a) check takes the screen's own b6 and GI history at its pixel (exact screen terms).
  - The screen's ls_box=2 box rule at hits when its box pass ran in mode 2 (unshadowed boxes traced to the
    fade-in end, shadow-mapped ones to the shadow view's near plane). With ls_box 0/1 the hit's box rule
    already matched the screen's, except atlas vs ray at ls_box=0.
- Counters 419-426 device-local (set 6 b0, group-reduced, copied after the pass), log `rf hit gi` (rf_stats=1):
  tier a GI term vs the screen's at the same pixels, ratio with and without the term, AO-ray screen / cache sources.
- Tests: build + 83/83 CTests pass, incl. `hit_gi_compose_test` (16800 screen-identity cases, the GI trace's
  per-ray rule, Monte Carlo), the bindings contract, the atomics lint (device), shader_variant_test (1482
  requests), settings_test. The 4 RF_HIT_GI files (plain/SHaRC/em) compile and pass spirv-val (DXC by hand).
- PENDING: the key-off SPIR-V byte-identity gate. The rf_hit_shade_shader_test run was stopped (a 40+ min blind
  recompile). Rerun it with tools/shader_identity.py once that lands, before any merge. The `--gi` contract check
  of the 192 compiled RF_HIT_GI modules is also not run yet.
- Expected (not measured): S8 0.925 -> 1.0-1.03 (mode 2), 0.98-1.03 (mode 1; misses gi_hit_direct/sun bounce);
  S6 1.089 -> 1.15-1.25: S6's excess is in the direct term (hit direct + ambient 0.073 vs screen diffuse 0.059
  which includes the bounce), not the GI. Cost: +0.02-0.1 ms rf pass; ls_box=2 up to +0.3 ms at S8.
- Live prepared (not authorized): m4_promotion m4p3/m4p4 add warmup-gi, diag-gi1 / diag-gi2 / diag-gi1-box2,
  cost-gi1 + shot-gi1 (predicted.json filled: +0.08% / +0.05%). Dry run passed: 21.9 min, 1960 MB.
  This branch carries worktree-agent-a80bee797afaacedd (gate audit, predicted.json) as a fast-forward base.
- In game: Experimental, Shaded hidden-surface reflections On, then this option; rf_debug=8 shows L_a red /
  L_b green (yellow = equal) at S8 Water Queens House.
