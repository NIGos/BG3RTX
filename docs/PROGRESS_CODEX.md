# BG3RTX progress - 2026-11-04

## Current user decisions

- Stock visible hair ships. Strands are a post-release update on Experimental.
- Fresnel stays Experimental, Off in presets: invisible on open water to the user.
- Temporal post is accepted for Ultra by visual verdict. Cost 0.152-0.153 ms;
  statistical tests remain inconclusive and float energy +0.1492% failed.
- One-ray Ultra GI and Tilesets are being qualified; current defaults stay until accepted.
- Rejected: three GPU candidates, Medium Half, water bits 2/4, glint, alpha spill
  (no overflow at S8), GI guards, current Sun and IP grass. SHaRC is rejected unless matched inputs exist.
- GPL-3.0-or-later plus NVIDIA exception chosen; no NVIDIA notification planned.
- Microsoft runtime uses static CRT; no VC runtime installer or DLL ships.
- Only RTX 5090 tested. Manual launcher acceptance is DPI only. Keep work lean.
- Open: game-data rights, remaining backlog deferrals, installer lifecycle, oversized Git
  blob remedy, ambient ratio/water appearance, final archive/upload and acceptance below.

## Current qualification receipts

- wf9 VALID 08:59: 17 gates, static unchanged, turning hits about 6.7% -> 17%;
  compose 0.154 ms absolute. No release blocker remains for wf9.
- rel2 High VALID 12:13 qualifies OMM/masks; rel1 Ultra VALID 13:47;
  rel3 stock hair VALID 13:51. Review retained stock-hair images separately.
- Exact-k Ultra accepted: S7 0.717 -> 0.555 ms (-23%); energy within OFF spread 0.89%.
- Strands: demote fault fixed; hv7 VALID 12:37 swaps on 2 draws. Close-up 0.80 < 0.891;
  coverage/orphan follow-up remains. This supersedes the morning first-swap blocker.
- Packaging/provenance, DPI, memory/transition and Act 2/3 acceptance remain open.
  Repeat integrated smoke only for a material release change. New lives need explicit steering.

## Earlier night evidence (historical)

## Night report, part 2 (04:00-07:45)

- Bundles 16-22 plus four tooling merges; main de413e94. ABI unchanged (26).
- Water Fresnel: wsf1 VALID 06:44 (281 s). Pool rim +43% ON, open water +1.8%, surface FS 0.10-0.15 ms;
  stays Experimental by user decision. [Result](../tools/live_batches/water_surface_fresnel/RESULT_wsf1_064436.md).
- Hair strands, found and fixed in sequence by hv6 lives: recursive scene lock (load exit), coverage queries
  never paired (0/0), probe at the card's own depth, rays mirrored vertically (clip y not negated, 0.66 m off).
- Hair now: strand rays hit; hv6 07:30 lost the device 2.5 s after the first real swap. The fault dump shows
  in-flight scene TLAS referencing a released BLAS (#552). Later hv7 supersedes this fault status; see current receipts.
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

## Stage 3 - Visible defects: qualified fixes kept

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving band fixed.
- AO self-occlusion is accepted: 50% of character pixels; 99% of self-hits below 5 cm.
- wf9 is qualified; see current receipts. Reflection edge/hit-distance keys remain Off.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa.

## Stage 4 - Qualification: stock release; strands post-release

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow=1. Two later smoke_ultra runs are reported VALID with zero cascade overflow.
- Earlier OMM camp: 1.17 G rays, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms. Later smoke: 1.23 G rays.
- Box S6: signed energy delta 0.0 within noise; unshadowed traces 369 M -> 0; local lights +0.13 ms.
- Pool alpha S6 coverage 14.8% -> 52.1%; GI trace +0.46 ms. Wind RGB S8 45.0% -> 63.5%; +0.67 ms.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Earlier secondary grooms cost about 40% less than native cards. This does not qualify primary strands.
- Primary strands remain Off in presets; post-release coverage and GPU safety remain unqualified.
- rr_post_temporal is accepted for Ultra by visual verdict. Earlier paired evidence has 10 pairs.
- Exact p range 0.171875-0.828125 vs 0.00416667; presented registration rejects 9/20, face support 0.
- Paired float energy +0.1492% fails; background join fails. Cost 0.152-0.153 ms; no statistical quality pass.
- User sees steadier DoF and accepted promotion; this does not change the failed/inconclusive gates.
- Rejected: CPU perf keys, rr_hair_resolve, hair GI culling (+9-20% energy). No new queue for these.
- IP grass stays off: +3 coverage points costs +1.9 ms and about +1.5 GB.
- Generated-row publication/recovery remains unqualified. GI guards are rejected.

## Stage 5 - Effects and performance: retained evidence

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far: S8 energy -2.01%, camp -2.42%; trace +3.2/+3.7 ms. One-ray Ultra GI is now being qualified; no promotion yet.
- Fresnel uses native foam coverage c plus clean BRDF (1-c), with finite/domain/partition checks.
- wsf1 VALID 06:44: rim +43%, open water +1.8%, FS 0.10-0.15 ms, compose 0.61-0.67 ms; no PT reference here,
  it remains Experimental by user decision.
- M5.6 step 1 water glint is reported rejected offline; retain evidence, do not queue it.
- Camp pak join proves 0 additional publication: all 22,686 unique observations fail recipe/cull.
- Unique observations are 5.655% of attempts and 3.762% of draws. Raw UV matches alone cannot publish.
- Need a compatible Tileset/HLOD-mask scene, e.g. S6, with full witness/resource/recipe/cull/basis gates.
- Compact v4: 40,983 rows/40.01 MiB. Native precedence remains mandatory; rt_hit_material_paks stays off.
- Water sparse-hit reflection 2.107 -> 0.530 ms, repeat 2.491 -> 0.531 ms; energy/coverage not qualified.
- wo1 must prove post-water float energy within OFF noise, preserved coverage and zero truncation in every ON.
- rt_compact_hit_state, rt_alpha_metadata_cache and rt_water_empty_tiles are rejected/Off.
- Offline replay is 36/36 byte-exact; empty-tile compose saving ~0.156 ms is exploratory only.
- Full tracing everywhere. Medium Half rejected: about 0.25 ms saving without quality acceptance.
- SHaRC is rejected unless matched inputs exist. Hit-GI AO needs instanced-hit inputs.
- Animated lava, HeightGradient, HLOD sidedness, texture publication and AS fragmentation remain open.

## Stage 6 - Tooling and release: repairs kept; acceptance open

- Fail-fast, preflight lint, 15-minute launch cap, measured-FPS windows and session fault invalidation are in place.
- Readiness handles cached/no-op keys and hotkey keys; every new affected build still needs exact completion.
- Five cached-wait replays pass: hv6 25 ms, hv5 79 ms, wsf1 66 ms, cp4 86 ms, Dialogue3 57 ms.
- Frozen lint corpus: 223 KB derived from 90 VALID runs; preflight still uses live corpus.
- Folded checks: 147/147 CTests, 26/26 active lints, 6/6 combined dryruns. These are offline receipts.
- REFUSED/CRASH rows and crash monitor are reported added; verify runner behaviour on real use without fault injection.
- GUI fixes cover 13 findings; settings 4,356 checks and bridge 904 passed. Manual check is DPI only.
- Version-matched DXC/Vulkan notices are resolved: five texts retained verbatim; packaging tests 3/3 pass.
- Licence, notification and static CRT choices are settled above; game-data rights remain open.
- Installer, memory/transition and Act 2/3 remain open. Only RTX 5090 tested; no release approval implied.

## Stage 7 - Backlog and cleanup

- Follow the current release handoff; strand fixes belong to the post-release update.
- Open branch: water-glint, evidence only. Earlier open-branch lists in the archive are historical.
- Audit rejected keys/generated-row supersession, inn material records, BLAS relocation and M5.5 steps 2-4.
- Keep current replay/gate inputs; remove worktrees only with the dedicated junction-safe script.

## Post-release RTX hair

- [Hair plan](RTX_HAIR_COMPLETE_PLAN.md) retains M0-M8 scope and earlier evidence.
- 877/1,217 conversions pass; Gale beard LOD1 remains open.
- Shadow crossings 3,000/3,000; tips exact at 1,600 samples/alpha; fibre furnace 1.0.
- These offline results do not qualify coverage, temporal quality, cost/VRAM or GPU safety.
