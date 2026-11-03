# BG3RTX progress - 2026-11-03 afternoon (main dc3ef899, ABI 26)

## User questions and decisions

- Open: GUI_AUDIT_20261102 decisions, project licence and NVIDIA notification (Streamline/NGX).
- Open look: soften the S8 water sun-shadow edge or keep the game's glint suppression in shade?
- Open, not blocking: rt_hit_emission 1 or 2? Ultra keeps 1 (hidden hits); 2 stays developer only.
- Next live batches need fresh explicit steering after their dependent offline checks. None is authorized here.
- Decided: box shadows qualify for Ultra; dialogue AO is acceptable; rr_hair_resolve and CPU perf keys rejected.

## Report 2026-11-03 afternoon

- Stages 2 and 3 done; water fix still pending qualification. Ultra OMM and box lights (honour shadow flag) qualified; smoke valid.
- Dialogue moving shadow fixed by default; cascade identity finds 16-18 duplicates/frame (was 0). Keep rt_cascade_static_only off.
- Water camera correction looks right to the user; corrected wf9 qualification and GPU cost remain pending.
- RTX Hair groom: converted styles work in game at about 40% less cost than native hair cards; native card path stays off (+2.3-6.3 ms RT).
- Hair/DoF shimmer cause found: replayed DoF/bloom weights flip each frame. rr_post_temporal fix awaits live qualification.
- rr_hair_resolve rejected by the user. CPU perf keys rejected. Neither is qualified for presets.
- Null water keys removed: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa. Reflection-only keys remain off pending valid evidence.
- Launcher fixed: experimental toggles now appear in Advanced; only Lighting/Surfaces/Performance/Memory/Advanced are shown.
- Try Ultra at S6 Grymforge and camp for box lights/OMM; Dialogue3 for the fixed shadow band; S8 pool with Q/E for water correction.

Full pre-consolidation text and 59 ancestry-verified fragments, verbatim: [11-03 archive](archive/PROGRESS_20261103.md).
Earlier history: [11-02 evening](archive/PROGRESS_20261102_evening.md) and its linked archives.
Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md). Stage order: [GOAL_NEXT.md](GOAL_NEXT.md).
Fragments describe their original evidence; this summary records later decisions and supersedes stale pending verdicts.

## Ultra preset and GUI (stages 1-3 done)

- Ultra: RT water + Refraction, hidden-surface shading + Bounce, emission Hidden hits, far-field bounce 6 m.
- Ultra also enables rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow remains 1; High/lower presets and base defaults retain their existing choices.
- OMM om4 camp: 1.17 billion rays compared, 0 not compared, 0 violations.
- OMM RT passes 8.65-8.75 ms vs reference 9.37-10.08 ms (about 1 ms saved).
- Box ba1 S6: energy within noise, signed difference 0.0; unshadowed traces 0 vs old mode 369 million/120 frames.
- Box local-light pass 0.45 -> 0.58 ms (+0.13 ms, RTX 5090); unshadowed art lights preserved.
- Integrated smoke valid with qualified OMM and box lights; OMM/water variant skip fixed with a pinned reference tuple.
- Earlier water chain 0.53-0.67 ms; hit emission +0.08 ms; gi_far GI trace +3.2 ms S8 / +3.7 ms camp.
- Automatic: hit-material recycle, unlocked capture, lean hooks, scene wait, FG letterbox and pool alpha rows.
- Launcher categories: Lighting, Surfaces, Performance, Memory, Advanced. Experimental controls live in Advanced.
- Launcher category validation now rejects categories the UI cannot show. Old INIs migrate through the settings model.

## Dialogue shadows and RTX Hair

- Moving shadow band fixed: cascade identity no longer depends on per-draw allocations; matches 16-18 duplicates/frame.
- Motion pairs show no band above RT-sun-off noise. rt_cascade_static_only drops every caster and stays off.
- User accepts dialogue AO self-occlusion: 50% of character pixels, 99% self-hits under 5 cm. No AO correction planned.
- Native hair cards remain off: +2.3-6.3 ms RT; earlier GI cost +3.4-6.9 ms. GI cull brightens +9-20% and was rejected.
- Groom conversion works for converted styles in game, about 40% cheaper than native cards; full style coverage is unfinished.
- Dialogue1 groom run: 5 instances, 1,500 matches/300 frames; RT 9.07 vs cards 10.2-10.57 ms.
- GI 12.51 vs cards 14.1-14.3 ms; refit +0.57 ms. These are scene measurements, not a universal cost promise.
- Converter: Shadowheart LOD0 656 cards/40,090 triangles -> 86,000 strands, 855,000 LSS segments; IoU 0.92-0.94.
- Groom uses procedural secondary queries; primary cards remain. UV joins support draws without mapped indices.
- Pool is capped at one quarter of the mesh pool; selected LOD loads on demand, unused entries evict after 120 frames.
- Fibre BSDF is an offline prototype; current Lambert GI is 3-6 times darker than the Chiang reference.
- Remaining groom work: converter quality, coverage and fibre scattering; no blanket preset promotion recorded.

## Hair/DoF shimmer

- Cause: replayed DoF/bloom weights flip each frame after RR. Fix candidate is rr_post_temporal on post-temporal.
- Reprojects and clamps DoF weights, resolve term and bloom; rejection uses fresh input; RR stays before post.
- READY_FOR_LIVE, off everywhere: 133 CTests and QA detectors pass; key-off identity 236/236, new entries 2/2 valid.
- Six fp32 history images cost about 250 MiB at 2560 x 1067; GPU ms and visible quality remain unmeasured.
- Prepared Dialogue3 and static S8 Ultra 0/1/0/1 batch checks both ON windows, linear energy and clean timing.
- User rejected rr_hair_resolve; older ef2/ef3 candidate verdicts in the archive are superseded.
- rr_post=0 changes post ordering and is diagnostic, not the selected fix.

## Water reflections

- Whole-water wf2-wf7 metrics were dominated by native glints; they cannot decide reflection-only quality.
- wf8 located the turning defect in compose: stale camera-relative TLAS makes screen hits fail depth matching.
- First shift used the wrong camera copy; the next subtracted incompatible camera buffers (static delta 294.8 m).
- Corrected rt_water_compose_shift subtracts cam2 bytes 208..223 on both sides; fixture static delta 0.
- User says correction looks right. Keep qualification pending until corrected wf9 static and Q/E windows pass.
- wf9 requires static average build-camera delta < 0.01 m, nonzero samples and every-window gates.
- Earlier compose cost 0.10-0.15 ms is not a measurement of the corrected candidate's cost.
- Mouse turns never produce smooth input here; use held Q/E. Compare toggles within one run.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa; requests 147 -> 135.
- rt_water_rf_edge and rt_water_rr_hitdist remain unknown/off; old whole-water verdicts were invalid.
- rt_water_hit_shade 1/2 adds world-space lighting to hidden/off-screen hits; remains experimental/off.
- Retained F-1 TLAS/material/light tuple supports compose; invalid retention preserves ordinary compose.
- water-hit-cost proposes equivalent sparse membership/cone/zero culling; live GPU savings pending.

## Material coverage and memory (stage 4)

- Traced frames retain hit-material capture; earlier traced energies had measured hit features off.
- Automatic pool alpha rows: S6 qualified 14.8% -> 52.1%, energy +1.06%/-0.38%, GI trace +0.46 ms.
- Wind RGB S8: bit-exact, rejected 0%; qualified 45.0% -> 63.5%, energy -0.00%, trace +0.67 ms. Kept in Ultra.
- Mask rows: S8 +0.65 points, +0.04% energy, +0.006 ms; inn +12.1 points, -0.80% energy, +0.92 ms.
- Generated rows stay off; genrows-gate fixes missing depth-source publication during parallel recording before rerun.
- IP grass stays off: +3 points qualified, +1.9 ms and about +1.5 GB. No promotion from the old metric.
- cov-paks2 adds COLOR_0/Tileset storage and device-local UV join; 41,815 eligible meshes, catalogue blocked on LSLib DLLs.
- Remaining exact-data gaps: animated lava flowmap, Tileset vertex colour, HeightGradient world height, HLOD two-sided.
- Mesh content identity and VT alpha hit-material work are on main; dependent qualifications remain separate.
- Stream caps: Ultra source 384 MB, deferred 13 x 32 MB, query basis 768 MB; allocate by use.
- Source-copy retirement saved about 0.4 GB. Capture reserves, mesh pool and AS policies are on main.
- Caster allocations survive zero-demand windows; shrink only after active smaller samples; VRAM guard still caps growth.
- Long-session VRAM was flat from cycle 13 (18.2-18.7 GB), records 411-417; not a release qualification.
- AS fragmentation remains: 4.2 GB retained for 0.25-1.4 GB live AS. Keep the existing trend baseline cycles 12-14.

## Reflections, GI and performance (stage 5)

- rf_hit_shade stays Experimental: native-ratio bias S8 0.925, S6 1.089, camp 1.097; +1.4-1.8 ms.
- rf_hit_gi stays off. Hit AO matches offline screen data; instanced-hit gap needs a suitable replay pack.
- No global ambient correction: native/PT S8 1.44, camp 0.84, inn 0.78 are inconsistent.
- gi_far 6 m: S8 -2.01% vs predicted -2.0%, camp -2.42%. Use float energy; gi_debug=2 saturates.
- GI NaN/Inf history fix falls back to fresh samples; non-finite motion is rejected before integer conversion.
- GI reprojection guards still need qualification; finite NGX motion-scale validation is on main (layer restart).
- SHaRC sh1 failed: energy -0.86% vs predicted +1.5%; 23.8% found, 10.1% occluded-cell hits, +0.75 ms total.
- SHaRC stays off; M6 proceeds only if it beats the PT reference without losing energy.
- One GI ray: 4.32 -> 2.02 ms, energy +0.11%; flicker comparison invalid (OFF/OFF null failed). Ultra keeps two rays.
- CPU perf keys rejected; no preset promotion or further qualification queue for these keys.
- gpu-opt1: compact GI hit state, alpha metadata cache, empty water-tile skip; all off, 36 byte-exact replay comparisons.
- rt-continuity and gpu-opt2 are open lanes at main dc3ef899; no implementation or live result recorded yet.
- GPU candidates need energy/coverage equivalence, clean pass timing and a real Ultra smoke before promotion.

## Tooling and release (stage 6)

- Shader migration done: 2,946 -> 147 variants before null-key cleanup; scc1 valid, 0 DXC on converted toggles.
- scc1 S8 RT 1.86 ms, GI 5.0 ms. New keys use specialization constants unless bindings/layout change.
- Preprocessed cache: header comment edit 2,892 compiles -> 0 in 35 s; self-check 4/4 identical.
- Heavy DXC runs serialize machine-wide; do not compile work that a planned build improvement makes unnecessary.
- Every ON window must pass; snapshot gates are events with 1 s settle. Give every job patterns and expect.
- No main commits during merge-green. No lives during merge-green or builds/compiles during cost windows.
- Release still needs keyboard/Narrator, high contrast/DPI, installer lifecycle, long sessions and acts 2-3.
- Diagnostics collects allowlisted sanitized snapshots for exact preview and user-chosen local ZIP export.
- Game data (1,677 materials, 125 templates, 1,174 textures) is generated on the user's PC, not shipped.
- Licence/package checks remain open; preserve the archive's historical receipts without treating them as release approval.
- Six fragments remain unfurled because their heading branches are not ancestors of main; docs/progress/README.md stays.
- This consolidation changes documentation only: no shader requests, ABI, game launch, git metadata writes or commits.
