# BG3RTX progress - 2026-11-04 night

## User questions and decisions

- Promote water surface Fresnel (rt_water_surface_fresnel) to Ultra? wsf1 VALID 06:44: all gates pass; pool rim
  +43% brighter ON (open water +1.8%), FS 0.10-0.15 ms. Look at the S8 pool rim for shimmer first:
  ON shots vary 13% vs 2% OFF. [Result](../tools/live_batches/water_surface_fresnel/RESULT_wsf1_064436.md).
- Promote rr_post_temporal to Ultra on the user's visual judgement? Keep it off pending that decision.
  Dialogue3 paired evidence is inconclusive: 12 sign tests p 0.17-0.83; energy +0.15% unproven; cost 0.153 ms.
- Choose the project licence and whether to notify NVIDIA for the beta. No notification has been sent.
- Decide game-data redistribution, SHaRC distribution form and Microsoft runtime terms before release.
  SDK notices do not grant game-data rights. Static CRT is selected; no VC runtime installer/DLL is shipped.
- Complete manual launcher checks: Apply/restart badges, keyboard/focus, WebView2 zoom, DPI, high contrast and Narrator.
- Open appearance choices: S8 water sun-shadow edge/glint in shade; ambient ratio 1.44; rt_hit_emission 1 or 2.
  Ultra keeps emission 1. Mode 2 remains developer only.

## Night report, part 2 (04:00-07:45)

- Bundles 16-22 plus four tooling merges; main de413e94. ABI unchanged (26).
- Water Fresnel: wsf1 VALID 06:44 (281 s). Pool rim +43% ON, open water +1.8%, surface FS 0.10-0.15 ms;
  promotion waits on the user's look (question above). [Result](../tools/live_batches/water_surface_fresnel/RESULT_wsf1_064436.md).
- Hair strands, found and fixed in sequence by hv6 lives: recursive scene lock (load exit), coverage queries
  never paired (0/0), probe at the card's own depth, rays mirrored vertically (clip y not negated, 0.66 m off).
- Hair now: strand rays hit; hv6 07:30 lost the device 2.5 s after the first real swap. The fault dump shows
  in-flight scene TLAS referencing a released BLAS (#552). Fix with the hair agent; hv5/hair_crowd/L2 wait.
- Hair core 1/3/4 (shadow crossings): identity 147/257 byte-identical, 107 equivalent, 3 main-side differences;
  cached recheck running on claude/hair-core-shadow.
- Tooling fixed from INVALID runs: readiness waits a full producer period (wsf1 2.0 -> 6.4 s), trace capture
  stall budgeted (VALID p95 2.72 s, wsf1 windows 12.0 -> 15.4 s), bursts arm after readiness, stale prefetch
  requests dropped, lint no longer double-counts settings latency (active 26/26).
- Hair job gates: strand gates required only when a coverage period reports swaps, plus a job-level gate that
  card and strand samples are both nonzero (no vacuous VALID).
- No crash dump this part; one device loss (hair, above). Game closed after each run.

## Night report, part 1 (to 04:00)

- Bundles 12-15 merged; smoke_ultra VALID twice (cascade buffer full 0.0, OMM 1.23 G rays, 0 violations).
- Camp pak join: all 22,686 unique candidates Engine_Default_HLOD_VT; legitimate publication 0%. Needs S6.
- Hair conversion 877/1,217; all companions except Gale beard LOD1. Water glint (M5.6 step 1) rejected offline.

## Evidence boundaries

- [Night archive](archive/PROGRESS_20261104_night.md) keeps the earlier 495-line file; branch details are in git
  history (fragments folded at de413e94) and in each TRACK doc.
- [Handoff](HANDOFF_SESSION.md) gives the next queue; [GOAL_NEXT](GOAL_NEXT.md) defines stages 2-7.


## Stage 2 - Shader specialization: done

- Migration merged; scc1 VALID, 0 DXC compiles on converted toggles.
- Requests 2,946 -> 147 -> 135 before later layouts. These are historical snapshots, not today's total.
- Bundle 6 gate: 262/262 requests, 28 hit-shade interfaces, 0 failures; full-resolution identity 243/243.
- New on/off keys use specialization constants unless layouts/bindings change. Retired IDs stay reserved.
- Open: first-use pipeline continuity/toggle latency. Readiness repairs do not prove rendering quality.

## Stage 3 - Visible defects: fixes kept, water qualification open

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving shadow band fixed by default.
- User accepts AO self-occlusion: 50% of character pixels; 99% of self-hits below 5 cm.
- Water camera correction looks right to the user; offline static camera delta is 0.
- wf9 still needs valid static delta < 0.01 m, nonzero samples and held Q/E coverage/energy evidence.
- Exploratory compose cost +0.009 ms is not qualification. Reflection edge/hit-distance keys remain off.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa. Do not restore their old jobs.

## Stage 4 - Qualification: integrated smoke kept; hair and temporal open

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow=1. Two later smoke_ultra runs are reported VALID with zero cascade overflow.
- Earlier OMM camp: 1.17 G rays, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms. Later smoke: 1.23 G rays.
- Box S6: signed energy delta 0.0 within noise; unshadowed traces 369 M -> 0; local lights +0.13 ms.
- Pool alpha S6 coverage 14.8% -> 52.1%; GI trace +0.46 ms. Wind RGB S8 45.0% -> 63.5%; +0.67 ms.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Earlier secondary grooms cost about 40% less than native cards. This does not qualify primary strands.
- Primary strands remain off in presets: rays now hit, but the first swap lost the device (released BLAS).
- rr_post_temporal stays off/Advanced. VALID 032335 has 10 pairs; all 12 primary sign tests miss significance.
- Exact p range 0.171875-0.828125 vs 0.00416667; presented registration rejects 9/20, face support 0.
- Paired float energy +0.1492% fails; background join fails. Cost 0.152-0.153 ms; promotion is unproven.
- User sees steadier DoF. Any visual-judgement promotion must be explicit; S8 quality is still missing.
- Rejected: CPU perf keys, rr_hair_resolve, hair GI culling (+9-20% energy). No new queue for these.
- IP grass stays off: +3 coverage points costs +1.9 ms and about +1.5 GB.
- Generated-row publication/recovery and GI reprojection guards still lack complete valid qualification.

## Stage 5 - Effects and performance: experimental candidates stay off

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far: S8 energy -2.01%, camp -2.42%; trace +3.2/+3.7 ms. Ultra retains two GI rays.
- Fresnel uses native foam coverage c plus clean BRDF (1-c), with finite/domain/partition checks.
- wsf1 VALID 06:44: rim +43%, open water +1.8%, FS 0.10-0.15 ms, compose 0.61-0.67 ms; no PT reference here,
  so the user's look decides.
- M5.6 step 1 water glint is reported rejected offline; retain evidence, do not queue it.
- Camp pak join proves 0 additional publication: all 22,686 unique observations fail recipe/cull.
- Unique observations are 5.655% of attempts and 3.762% of draws. Raw UV matches alone cannot publish.
- Need a compatible Tileset/HLOD-mask scene, e.g. S6, with full witness/resource/recipe/cull/basis gates.
- Compact v4: 40,983 rows/40.01 MiB. Native precedence remains mandatory; rt_hit_material_paks stays off.
- Water sparse-hit reflection 2.107 -> 0.530 ms, repeat 2.491 -> 0.531 ms; energy/coverage not qualified.
- wo1 must prove post-water float energy within OFF noise, preserved coverage and zero truncation in every ON.
- rt_compact_hit_state, rt_alpha_metadata_cache and rt_water_empty_tiles remain experimental/off.
- Offline replay is 36/36 byte-exact; empty-tile compose saving ~0.156 ms is exploratory only.
- Low/Medium rt_trace_scale=0.5 lacks quality gates; High/Ultra stays 1.0. Medium RT saving ~0.18 ms is exploratory.
- SHaRC stays off: no PT superiority/cache-history evidence. Hit-GI AO needs a pack with instanced hits.
- Animated lava, HeightGradient, HLOD sidedness, texture publication and AS fragmentation remain open.

## Stage 6 - Tooling and release: repairs kept; acceptance open

- Fail-fast, preflight lint, 15-minute launch cap, measured-FPS windows and session fault invalidation are in place.
- Readiness handles cached/no-op keys and hotkey keys; every new affected build still needs exact completion.
- Five cached-wait replays pass: hv6 25 ms, hv5 79 ms, wsf1 66 ms, cp4 86 ms, Dialogue3 57 ms.
- Frozen lint corpus: 223 KB derived from 90 VALID runs; preflight still uses live corpus.
- Folded checks: 147/147 CTests, 26/26 active lints, 6/6 combined dryruns. These are offline receipts.
- REFUSED/CRASH rows and crash monitor are reported added; verify runner behaviour on real use without fault injection.
- GUI fixes cover 13 review findings; settings 4,356 checks and bridge 904 passed. Manual acceptance remains open.
- Version-matched DXC/Vulkan notices are resolved: five texts retained verbatim; packaging tests 3/3 pass.
- Project licence, NVIDIA notification, data rights, Microsoft terms and SHaRC distribution remain release decisions.
- Disposable-VM installer, long sessions, acts 2-3 and other GPUs remain open. No release approval is implied.

## Stage 7 - Backlog and cleanup

- Follow the handoff queue before remaining READY_FOR_LIVE jobs; no speculative research ahead of the hair fix.
- Open branch: water-glint, evidence only. Earlier open-branch lists in the archive are historical.
- Audit rejected keys/generated-row supersession, inn material records, BLAS relocation and M5.5 steps 2-4.
- Keep current replay/gate inputs; remove worktrees only with the dedicated junction-safe script.

## RTX hair milestones M0-M8

- [Complete hair plan](RTX_HAIR_COMPLETE_PLAN.md) defines the scope; its milestone table predates later night updates.
- M0 stability: bundle15 lives, including hv5 reloads, reportedly survive. Primary participation is still blocked.
  Strand rays hit since bundle22; the first swap lost the device (released BLAS #552). Fix before hv5/hair_crowd.
- M1 conversion: 877/1,217 pass, confirmed as the coverage-bound cohort; companion completion is a supplied update.
  Converter 2.4-3.4x faster, renders 1.8x, bit-identical; 80-style representative set, GR2/groom caches.
  LOD0-to-LOD1 transfer rejected by coverage/IoU gates. Gale beard LOD1 remains unresolved.
- M2 shadows: transmittance offline verified, rt_hair_groom_shadow_t off; shadow-map consistency and AO open.
  GPU bench: 3,000/3,000 crossing counts match; transmission error 1.25 sigma. L2 live/cost still missing.
- M3 image: dithered tips offline verified, off; 1,600 samples/alpha give exact expected shares.
  Sub-pixel coverage, RR guides, DoF/fog/water depth and full temporal/energy quality remain open.
- M4 indirect: fibre-GI offline candidate; white furnace 1.0, coloured errors <= 1.3882%, GPU/CPU 1.46e-7.
  Full suite was 145/146 with inherited water identity failure; no complete acceptance or live/GPU cost claim.
  Specular indirect, many-light fibre sampling and strand radiance-cache integration remain open.
- M5 views: portraits, dialogue/small passes, instanced hair, reflections and water-hit fibre shading remain open.
- M6 performance: half-resolution traces and crowd budgets remain unqualified; no measured primary strand cost.
  SER does not apply to these inline queries. Native-card cost is not a strand budget receipt.
- M7 content: 877-style cohort exists; all companions except Gale beard LOD1 reported converted; dyes need live A/B.
- M8 qualification: not done. Need visible strands, every-window gates, cost/VRAM, real Ultra smoke and user check.
  Keep unqualified keys off in presets; run hv4 GPU-AV after the participation and L2 prerequisites.
