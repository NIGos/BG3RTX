# BG3RTX progress - 2026-11-03 afternoon (main eca2023b, ABI 26)

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

## Water interface / column split (branch water-shader-analysis, READY_FOR_LIVE, 2026-11-03)
- rt_water_surface_fresnel defaults 0 in every preset; visible Surfaces control, restart to first enable, live toggles after provisioning.
- Pool FS c4e92ccdb08dd777, foam SSA 751 == 0 only; unsupported families and foam pixels retain native shading.
- SpecId 1025; interface A/B independent of opacity, column weighted by 1-F; no reflection trace/hit shade edits or record growth.
- Build PASS; 144/144 CTests pass (166.11 s), all 7 candidate FS forms valid; CPU 712,551 checks incl. 5,544 partitions, settings 4,200 pass.
- Shader gate PASS: 2/2 raw ordinary requests identical; 16/16 compose arithmetic-OFF identical to HEAD, 16/16 production valid, no missing requests; capability raw modules change.
- shaders/variants.txt request delta 0; default FS patch words unchanged. No commits or Git metadata writes.
- Prepared S8 Ultra pool-rim static/Q/E same-run A/B: lint + fake dryrun pass, one launch, 4.3 minutes, estimated 490 MB; not queued/run.
- GPU compose cost unmeasured; inspect shallow rim reflections and post-water float colour against reference partition; no equal-mean gate or quality pass.

## Water surface analysis (branch water-shader-analysis, planned, 2026-11-03)
- Audited three S8/S1 FS dumps, Materials.pak templates, compose/reflect/hit shading and retained S8 evidence offline.
- Native AA already exists: roughness floor .09, derivative variance cap .18; reflection is incorrectly coupled to opacity.
- Exact/Schlick F at 80 degrees: .3454/.3974; pool template T plateaus at .5; no physical RGB extinction mapping.
- Confirmed historical S8 shadow removal 46.665% of mean C, clamped 0, compose .113 ms; no isolated glint-energy claim.
- Added M5.6: direct glint, interface split, path ramp, spectral medium, foam; all future keys off, costs estimated.
- See docs/WATER_SURFACE_ANALYSIS.md; live S8 pool static/Q/E A/B still needed. Old 113740 pack absent; GI pack lacks water inputs.
- Docs only: no game, git metadata writes, commit, shader/ABI changes or variant request delta (0).

## Water hit cost (branch water-hit-cost, READY_FOR_LIVE, 2026-11-03)
- No commit, Git metadata writes, game launch or real queue submission. ABI unchanged; request delta 0.
- rt_water_hit_cost, SpecId 1021, default 0: bit 1 sparse slots, bit 2 cone/zero, bit 4 slot-order K; 7 combines them.
- Experimental GUI exposes individual steps; presets off. Pipelines cached for A/B; original loop isolated when off.
- Sparse traversal: <=4096 membership reads/hit -> <=128 mask reads + N active reads; +512 B device memory.
- At illustrative N=39: 167 vs 4096 reads (24.5x fewer), not a measured GPU speedup.
- Prepare adds one mask atomic per unique active slot, not per duplicate froxel occurrence.
- Cone before falloff saves one pow on an excluded spot; zero incident skips BRDF, not extra visibility rays.
- Slot order removes <=496 insertion moves/nontruncated hit; effective K remains min(reaching,32), overflow retains probe.
- Material already fetched once and shared by local/sun/sky. No redundant fetch removed.
- Positive cutoff deferred: encoded half-ULP does not bound composed/exposed error. Exact zero discards no energy.
- Temporal reuse deferred without energy/stability evidence; material/cluster hit binning needs a new work list.
- Build passed; all 132 CTests passed, including 10,000 randomized CPU incident comparisons.
- pp-diff wf9-compose-shift: 236 common requests, 145 input-identical, 80 pp-identical, 11 changed.
- waterhit --sample 16: 9/9 layouts, 96/96 comparisons, 9 key-off baseline matches; active modes 1/2: 64/64 matches.
- watercost --sample 0: both files, 60/60 comparisons; prepare 3/3 valid, both states and key-off baseline match.
- Dryrun passed: 349 s, 32 captures, 1930 MB; each bit and combined 0/value/0/value at fixed hit shade 2.
- S8 Water Queens House, Ultra: pool column reflections; static float energy, reflection GPU median/p90, compose separately.
- Supplied cost 0.15 ms -> 0.8-2.4 ms median, p90 up to 3.3 ms; candidate cost unmeasured. Budgets are targets, not stops.
- No quality pass or preset promotion. Every ON still requires energy within OFF noise, preserved coverage and zero truncation.

## Water capture tooling (branch water-capture-fix, offline verified, 2026-11-03)
- Energy: dump slot 0 after the core water hook; pair explicit post_core events with the same pass/image.
- Coverage: use published 32/40/48/96/104/112 B strides, pack word 7's marker, decode exact R8_UINT; transport stays strict.
- Fixtures cover missing/wrong-image readbacks, native-water cancellation, nonfinite counts, every stride and truncated R8 data.
- Re-analysis: all 32 wo1 + 4 wf9 energy traces still lack post-water data; all four evening 99100-99103 bursts lack coverage and remain INVALID.
- Validation: build + 144/144 CTests passed (118 s); QA detectors passed; offline report: build/water-capture-fix-reanalysis.json.
- No game launch, commit, ABI/settings/shader change; variant request delta 0. Restart needed for host fix; live qualification remains paused.

## Runner and offline tool repairs (branch tools-review-fixes, kept, 2026-11-03)
- Fixed slice phase/label rebasing, explicit queue completion, and owned merge recovery with abort/reset verification.
- Fixed single-value warmup and startup checks, shared midnight origin, shared plan fields, and prerequisite preservation.
- Regression checks: 25 Python tests and 3 new synthetic PowerShell tests pass; combined dryrun passes all 3 manifests.
- Full lint: 104 jobs, 75 failures, same failing count as baseline; every previously passing audited job still passes.
- Re-extracted 2 Dialogue3 runs offline; original forbidden-log failures remain. Camp extraction rejects manifest drift.
- Existing queue tests: 6 pass, 2 blocked by missing py/pwsh; existing camera test fails its water turn-window assumption.
- No game, real queue, commits or repository Git metadata changes. Shader request delta 0; offline receipts in captures/offline/tools-review-extracted.
- 18:56 Water run: all 6 slices INVALID; measurements and limits in combined_20261103/RESULT_WATER_185612.md (no qualification).
- Source generators extend go2 shots to 17 s and wo1 to 18 s; all gates retained. Lint budgets serialized traces per source segment.
- wf9 transport INVALID is a real missing-coverage capture defect; water energy lacks post-water scene dumps. Both remain unresolved.
- Exploratory savings: empty tiles compose ~0.156 ms; combined hit-cost reflection ~1.574 ms / compose ~2.498 ms; temporal ~0.12 ms.
- 27 unit tests, 7 job lints and all 3 combined dryrun manifests pass; no game or Git metadata writes. Shader request delta 0.
- Default fail-fast rejects forbids, failed windows and silent producers; combined failures skip slices with explicit incomplete status and normal settings restoration.
- Queue preflight lints the actual plan, reports duration and refuses launches above 15 minutes without max_minutes; oversized water/cold-camp combinations remain blocked.
- Derived window budgets and startup producer checks refresh active generators; post 173 -> 92.5 s, GPU2 176 -> 118.2 s; all job deltas: tools/live_batches/duration_review.json.
- Offline checks: 18 active lints, 3 combined dryruns, 32 flow + 2 generator + 15 guard + 17 hair tests pass; available queue/restore PowerShell tests pass.
- No game, real queue writes or Git metadata changes; cancellation/native py and child-permission/pwsh checks remain environment-blocked; shared-marker Git-writing test skipped.
- Review fix: retain scene-settle bounds (su1 45 s), measured first-load floors 16-29 s, hair toggles 5 s, post-reload 26.1 s and hv5 60 s per reload.
- Fixed edge fixture millisecond carry; readiness, survival and continuous-window regression checks added; duration review and active artifacts regenerated.
- Full build and all 146 CTests pass; 19 active live-flow lints and all 3 combined dryrun manifests pass. Shader request delta 0.
- Defender history flags the main-checkout diagnostics executable; no triggering behavior found, freshly rebuilt worktree fixture passes. No security changes, game launch, commits or Git metadata writes.

## SHaRC evaluation (branch sharc-eval requested, KEEP EXPERIMENTAL OFF, 2026-11-03)
- No PT superiority established: retain sharc=0; no promotion or removal.
- Five packs (camp, S8, inn, two S6) have one frame each, no set 4 cache state/history.
- Replay writer rejects SHaRC; repeat dispatches cannot measure cache temporal variance.
- Native/PT far baseline only: camp bias -15.61%, RMSE 0.337235, energy 0.843932;
  inn -21.95%, 0.100429, 0.780487. These are not SHaRC ON/OFF results.
- Retained S8 PT lacks roofs; S6 PT outputs and regional masks absent. ON metrics unavailable.
- Historical S8 cost re-read: trace +0.494 ms, reflections +0.168 ms; whole GI +0.810 ms.
- Recorded -0.86% energy vs +1.5% prediction cannot be re-qualified: dumps missing, gate INCOMPLETE.
- PT self-test 11/11, validation errors 0. See docs/SHARC_EVAL.md and captures/sharc-eval/.
- No game, queue or git writes; supplied pak-textures checkout and existing gi.cpp change preserved.
- No shader/ABI/settings change; variant delta 0; no promotion job. Cache history is the blocker.

## RT continuity and water tilesets (branch rt-continuity, READY_FOR_LIVE, 2026-11-03)
- Trial log: GI/sun/water waits at 15:02:12; local match/box waits at 15:02:13.606; tileset skip at 15:02:24.006.
- Log selected + Tilesets at 15:02:23.981. Hair + scalp alone writes tilesets off; clarified the description.
- GI, sun, local shadows and water retain the previous compatible pipeline while replacements build or fail.
- GI retains complete specialization keys; water prepares both hit layouts and falls back to ordinary water while cold.
- Water supplies native tileset constant 1008 instead of skipping. ABI/settings defaults/presets unchanged; shader requests +0.
- Visible live Experimental choices prewarm below normal priority once real layouts exist; demand outranks queued prewarming.
- Startup count: 1 RR capture + 12 RR post pipelines; GI/water extras and cumulative count are logged per entry/layout.
- Dev off extras: GI 1 per entry; water 1 hair, plus 1 compose-shift per compose entry and 2 hit modes per hit entry.
- Dev on also warms live hair specializations, 4 exact-k and 2 reference entries. Choices warm relative to startup; new combinations use continuity.
- Shader cache avoids repeat DXC; driver pipeline creation can still cost time. No game launch or git metadata writes.
- Checks: build + 132/132 CTests passed; pipeline selection 342 checks, 25 repeat passes. Identity: 236/236 unchanged, zero compiles needed.
- Water native gate: 19/19 requests, 152/152 comparisons; combined hit/shift: 9/9 requests, 96/96 comparisons. No shader source changed.
- Live continuity/first-toggle latency and GPU cost are unmeasured. Check S8 pool/native coverage after a separately authorized launch.

## Trial follow-up (rt-continuity, READY_FOR_LIVE, 2026-11-03)
- User trial 15:07-15:10: reflection 73.7M hit attempts (11.1M hidden, 14.3M offscreen), compose 0.
- Host accepts F-1 only by borrowing an existing exact slot/generation query; current-frame camera checks remain.
- Borrowing recordings pin query storage and slot resources until every query user retires; CPU lifetime tests added.
- Water selects its separate ordinary/hit pipeline cache before layout validation; ordinary layouts ignore inactive hit sets.
- Native tileset INI edits wait for restart in the layer; launcher reports the selected choice apply mode. Hair/scalp stay live.
- Hit-shade text warns of about a minute compiling on first enable. ABI/defaults/presets unchanged; shader requests +0.
- Validation: build + 132/132 CTests passed; schema regenerated. PP diff main: 236/236 unchanged, 0 compiles; diff check clean.
- No game launch or git metadata writes. Layer restart required; S8 compose attempts and GPU cost remain unmeasured.

## Temporal DoF/bloom replay (branch post-temporal, READY_FOR_LIVE, 2026-11-03)
- Added rr_post_temporal, live GUI control, off by default and in every preset. No ABI change.
- One render-res pass reprojects A/C/bloom with RR MV and jitter; alpha 1/16, current 3x3 clamp.
- Per-tap depth/MV rejection uses D24/fp16 precision and pixel footprint; failure selects fresh input.
- NGX reset/mode switch, skipped frames, toggles and render/output resize reset history.
- Final aligns AC/EB at uv + jitter/render size. Exposure stays fresh; no host-visible atomics.
- Six device-local fp32 images cost ~250 MiB at 2560 x 1067; GPU ms await the prepared live batch.
- CPU reference: constant/linear energy, alternating A mean 0.5 and 961x variance reduction;
  iid flipping variance falls 31x; disocclusion, NaN recovery, clamp and depth/MV checks pass.
- Build and all 133 CTests pass after fixing the redundant manifest entry; QA detectors pass.
- pp-diff main 5f17b553: 219 identical, 1 pp-same, 16 water differences from newer main.
- Byte identity HEAD d715c7f4: 236/236; rr_post vs main: identical. New entries: 2/2 valid.
- Manifest delta 0; standalone request +1 (2 entries), auto-discovered. No define multiplication.
- tools/live_batches/post_temporal: Dialogue3 and static S8, real Ultra, 0/1/0/1, separate K8/timing.
- K8 requires effective AC/EB; selective float traces retain AC/Z/EB for linear energy checks.
- Fake queue dryrun passes INVALID-with-gates and valid-with-gates-stripped. No jobs staged.
- Supplied hair residual 7.6/13.1 -> target 3.1; bright contours 7.3/6.1 -> target 1.5 (PQ x1000).
- Far-background float mean and stable face must stay within OFF-repeat noise in both ON windows.
- Check white hair over DoF and sun-lit rims in Test Dialogue3; quality and GPU cost remain pending.

## Pak join participation (branch pakjoin-zero, READY_FOR_LIVE, 2026-11-03)
- Cause: discovery required the HLOD-only fallback; camp instead logs 306-308 class-A draws/frame. Readback also filtered by HLOD publication eligibility.
- rt_hit_material_paks stays off by default; enabled, it probes keyed pool draws. Pooled recipes may use bounded retired UV readback after basis capture; captured and instanced rows retain precedence.
- Shared compact v4 validates: 40,983 rows, 41,956,440 bytes. No generator/index fix, content-ID dependency, ABI or shader request delta.
- Camp prediction after layer restart with the key enabled: cumulative draws delta 36,720-36,960/120 frames; UV attempts delta 1-36,960 after readbacks retire. Zero is a failed participation gate.
- Full witness/recipe/cull/resource/basis gates stay mandatory; exact rows may remain 0 (retained camp replay: 58 unique UV candidates/471, 0 full witnesses).
- Host DLL builds; five focused CTests pass; C++ parses the real v4 table; diff check clean. No game launched, git metadata writes, coverage promotion or GPU cost measurement.

## Live-test design checks and same-save composition (branch live-flow, offline verified, 2026-11-03)
- Shared live_flow.py checks cached recent VALID logs, complete-line window budgets and pre-measurement warmup.
- lean_dryrun runs the lints before cache reuse; matched forbids quote the offending VALID line.
- Corpus: 21 logs / 9 saves, newest 3 readable logs per save; 32 old VALID rows lack a resolvable save.
- All 89 prepared jobs audited: initially 84 failed; after known repairs 82 fail and 7 pass.
- Per-job findings and regeneration command: tools/live_batches/live_flow_audit.md; assumptions: LIVE_FLOW.md.
- Scoped load-time mesh-pool forbids to measured-window event gates; narrowed nonfinite/crash patterns.
- post_temporal: both values warmed before measurements; timing 10 -> 12 s, derived minimum > 11 s.
- Both post_temporal jobs pass shared lints and the complete fake-queue dryrun (A invalid, B valid).
- combine.py groups compatible jobs by save, resets settings, prefixes gates and retains source manifests.
- Extraction restores per-batch labels/indices, validates each slice and runs its original post gate.
- Existing inn batches cb3 + cb7: 2 launches -> 1, 29 phases; combined design dryrun passes.
- Tests: 18 live-flow regressions and existing queue_gates_test.py pass; PowerShell parses; diff check passes.
- No shader request delta, ABI change, game launch, queue staging, commit or git metadata write.
- Remaining jobs need their reported warmup, cadence/FPS or regex assumptions fixed before queueing.

## Layer review fixes (branch layer-review-fixes, kept, 2026-11-03)
- OMM allocates stats/readback before pipeline publication and the fast path; partial failures retry independently.
- Shader entries retain replaced SPIR-V allocations until process exit so async pipeline workers keep valid blobs.
- Water compose records always allow transfer-source copies when pan_water is enabled later.
- Build passed; all 144 CTests passed (178.79 s). ABI unchanged; shader request delta 0; no game launch.
- No commit or git metadata writes. Retired shader memory grows only on successful shader replacements.

## Launcher release QA (branch launcher-qa, fixed with acceptance open, 2026-11-03)

- Fixed visible custom preset reset, native custom detection and choice-specific Apply badges.
- Requirements now show every prerequisite; save review enforces them before persistence.
- Fixed Developer authorization, focus retention, Enter toggles, forced colors and zoom reflow rules.
- Every card states GPU cost or Not measured; flat text surfaces preserve contrast.
- Contrast minimum: token text 6.68:1; Play 5.52:1. Bridge: 695 checks passed.
- Added offline shipped-JS regression plus CSS contrast/reflow contracts to CTest.
- Full build and all 134 CTests passed. git diff --check passed.
- Open: real WebView2 zoom 100/150/200%, Windows high contrast and Narrator acceptance.
- PRODUCT_REVIEW absent locally; see docs/release/LAUNCHER_QA.md for the checklist.
- Check in launcher: review presets, Advanced > Experimental, requirements, Tab/Space/Enter focus.
- No game launch, settings write or Git metadata changes. ABI/shader requests 0; renderer cost unchanged.

## Live job lint repairs (branch jobs-lint-fix, READY_FOR_LIVE, 2026-11-03)

- No game launch, runtime edits, queue submission, commit or git metadata writes.
- wo1: shot windows 12 s, cost windows 14 s; prewarm 0/1/2/4/7 before shots; held Q/E unchanged.
- wf9 warms compose shift; active jobs gain missing warmups, cadence budgets and scoped load-warning gates.
- All 19 active source jobs pass shared lints and their batch dryruns; generators updated where present.
- rr_post_decision and dialogue_shadows marked historical; anim_materials is absent from checkout/audit.
- Combined S8/camp/Dialogue3: 10 launches -> 3; 110/38/62 phases, 1465/546/398 s before loading/waits.
- combine.py restores each source preset (including Medium/High), baselines and independent warmup.
- Combined lints and synthetic extraction pass for all 10 slices; fake logs fail real gates as intended.
- Shared dryrun Python fallback fixed; live_flow regression tests 19/19 pass. Shader request delta 0, ABI unchanged.
- Reproduce: combined_20261103/make_jobs.py then dryrun.py; source batch dryrun.ps1 scripts.
- Live evidence and GPU costs remain unmeasured. Draft jobs stay outside watched pending inboxes.
- 18:03-18:25 combined runs were INVALID: oversized context, disabled pool stats, and cold baseline shaders.
- Runner context now uses a file; actual oversized Water payload passes the synthetic child test; env lengths are linted.
- cp4 generator enables rt_stats only for observations, warms paks 0/1, and waits for prefetch/GI/water readiness.
- Camp full/half reflections wait for distinct ready pipelines; baseline compiles ended 18:08:33, prefetch 18:08:48.
- Python regressions 23/23, combined dryrun (3 saves/10 slices), combined/compact/edge lints and queue safety checks pass.
- hair_validation is absent (lint fails: no jobs); separate hair_qual audit has 4 existing failures. No live rerun or commit.

## Native hair identity and load admission (branch hairline-band, offline candidate, 2026-11-03)

- Class 5 cannot distinguish Hair/Pubes from Scalp; the existing receiver guard was bypassed by default.
- Physical-origin publication now always rejects competing native Scalp recipes in the existing depth interval.
- Native root coverage is unchanged: Scalp smoothstep(A) * (1 + (B - 1) * root); Hair A * (1 - B * root) * angular * density.
- No darkening, new epsilon, feature key, ABI change or shader request delta (0).
- hq4 14:06:33: optional pool upload failed; 1857.8 / 2047 MiB sampled, 466 new meshes, 264 evictions, 60/60 split builds.
- Required procedural bounds now precede optional uploads; native cache admission includes pending pool and AS ownership.
- Deferred cache extraction retains the captured dynamic draw; pool cap and retirement fences are unchanged.
- Build and all 133 CTests pass; receiver has 58 checks; pool admission adds 9 budget/cap/overflow cases.
- Shader identity: 235/237 identical; 2 intended origin changes validate; native specialization 288/288, key-off baseline 58/58 pass.
- git diff --check passes; progress is UTF-8 without BOM, 14 lines. Native shader request delta 0.
- Historical trial had no F10 packet/A/B; visible band attribution and actual Dialogue3 peak remain unverified.
- READY_FOR_LIVE. No game launched, preset promoted or Git metadata changed. GPU cost unmeasured.

## RTX Hair automated validation (branch hair-validation, offline only, 2026-11-03)

- Added per-character coverage, silhouette, depth/duplicates, motion/trailing/flicker and energy/PT gates.
- Bounds use OFF-repeat Student prediction intervals plus pixel/depth precision; absent evidence is INVALID.
- Prepared combined Dialogue1/camp/S8 jobs: 297 s each, held Q/E, zoom, six toggles, K8/P16 and Ultra costs.
- Added selected-save F8 reload with sampled lifecycle evidence; live behaviour remains unverified.
- Dedicated 50 s GPU-AV job; launcher resolves local SDK manifest/DLL. No local validation layer found.
- 17 synthetic hair tests, 5 reload parser checks, existing QA detectors and 16 edge-flicker tests pass.
- Four-job dryrun: lints/budgets pass, four expected INVALID negatives and four valid structural positives.
- Estimated capture budget 1,480 MB; shader request delta 0, ABI/defaults unchanged.
- Historical 17:01/17:04 reports both FAIL: device-fault indicators, CPU exception and pool exhaustion.
- Missing runtime-owned mask/MV/fresh/FG/PT evidence and observed 1/5/10 cohorts block qualification.
- See docs/HAIR_VALIDATION.md; no game, hair runtime, saves or Git metadata changes. No commit.
- Offline job repair: removed capped-pool forbids; declared the producer's 300-frame cadence and warmed both primary values before shots.
- Kept 600 warmup frames and 750 frames per cost window; regenerated all four jobs/plans with no duration increase.
- Dryrun exposed the missing reload_selected schema field; added boolean validation and focused reload parser checks, all passing.
- Verification: 36 Python tests PASS; live-flow 4 jobs, 0 failing (was 4); dryrun all checks PASS, 4 expected INVALID negatives/4 valid positives, 1,480 MB. No live qualification.

## Lower-preset trace scaling (branch gpu-opt2, offline candidate, 2026-11-03)
- Baseline dc3ef899939b; no commits, git metadata edits, game launches or real queue submissions.
- Added rt_trace_scale: half-width/height GI and reflection traces, 1 RT spp, full-grid reconstruction and RR guides.
- Low/Medium use 0.5; High/Ultra stay 1.0 per PERF_AUDIT_GPU. Low preserves screen-space GI 2 rays and reflections off.
- Far/bounce/emission remain off below Ultra; existing levels are unchanged and protected by settings tests.
- Scale 1.0: 236/236 baseline shader requests byte-identical. ABI unchanged.
- Manifest 145 -> 163 (+18): reflection binding 31 requires a layout variant; scaled requests 19/19 compile/validate.
- Corrected synthetic replay, 32 frames: GI mean error -3.94%, within noise band +/-10.16% (4 paired SE); matched pixels exact.
- Initial seed sweep INVALID: material epochs did not follow seeds; fixture corrected and zero-energy frames now fail.
- PT furnace 4.996864 vs 5.0 (-0.063%, SE 0.012); replay/PT CTests and 5 targeted CPU/tool CTests pass; core builds.
- Prepared gpu_opt2 batch: camp High, S8 Medium, one launch each; 1.0/0.5/1.0, held static shots, complete 300-frame costs.
- Dry-run passes: 270 s phases, estimated 1,100 MB. predicted.json is uncommitted as requested.
- Actual GPU ms UNMEASURED (audit prediction: 0.4-1.3 ms saved); real-scene/reflection/RR energy and thin-detail quality pending.
- Check camp foliage and S8 reflections; analyze float pre-post energy against same-run OFF repeats. No quality promotion claimed.

## Exact GPU candidates (branch gpu-opt1, READY_FOR_LIVE, 2026-11-03)
- No game launch, queue submission, commit, merge or other git metadata write. ABI unchanged.
- Off in every preset: rt_compact_hit_state (1024), rt_alpha_metadata_cache (1022), rt_water_empty_tiles (1023).
- Compact hit: defer transform fetch past traversal; omit only proven non-pool transforms; reuse GI normal and immutable light ranges/scale.
- Pool matrix storage remains; removing it needs a different transform source. No precision, ray or sampling change.
- Alpha: lazy invocation-local descriptor size/count/stride/policy; native LOD, cutoff, stochastic threshold and UNKNOWN retained.
- Water: skip only groups with zero existing validity predicates; native colour, dispatch count, camera forwarding and ownership retained.
- Shader request delta: 0. Separate specialization pipeline slots preserve warmed OFF/ON states.
- Alpha production helper: 3,840,000 exact checks, 10,000 descriptor epochs, pass. Water control model: 1,065 groups, pass.
- Final build + all 134 CTests PASS (93 s); initial settings/schema and exposure warm-up failures fixed. Live analyzer has 6 regression tests.
- GPU replay: 9 production fixtures x 4 ON comparisons pass byte-for-byte (GI/depth/stats; native water target/records/camera/stats).
- Participation: 4,096 transformed pool hits; solid/clear alpha; water 39 owned/26 traced, 39 invalid; empty native packed colour unchanged.
- Key-off gate caught/fixed a duplicate OFF bounds check; unused subgroup capabilities trimmed symmetrically after specialization.
- Key-off shader gate PASS: 173 requests/194 entries same canonical program as HEAD after specialization; SPIR-V validation passes.
- Hair hq1 13:32 INVALID: warm exact cards/origin-off then full hair before shot-cards; every-window no-build gate remains strict.
- Unqueued tools/live_batches/gpu_opt1: camp + S8, one 300 s run each, isolated ON states bracketed by OFF; static float/PNG identity and 300-frame costs.
- predicted.json: planning only; compact camp trace 5-10%, alpha ceiling 2-5%, empty WI compose 0.08-0.16 ms; guaranteed saving 0 ms.
- GPU cost unmeasured. Empty WI/inn follow-up needed if camp/S8 lack empty views; smoke_ultra with qualified keys still required.
- In game after explicit live steering: camp foliage/pond and S8 pool must retain thin cutouts/native colour and equal OFF-repeat-bounded scene energy.

## Generated-row race and phase gates (branch genrows-gate, READY_FOR_LIVE, 2026-11-03)
- Generated AlphaTest2s now captures when depth has not recorded yet; no_source still counts misses.
- Final joins reject absent/duplicate sources and IP_Base_VT; IP grass keeps its source gate.
- Regression covers G-buffer before depth, late unique mapping, missing and duplicate sources.
- Prior regression: camp 163/0/0 seen/captured/mapped, inn 86/0/0; live recovery unmeasured.
- Cost caveat: historical camp deferred stream 57 -> 93 MB; new GPU ms need live evidence.
- Phase wait_log searches the latest matching line per regex across this run, including cached records.
- Waiting pauses phase time; timeout aborts. Recenter posts pointer centre, prints its marker, no Home key.
- lean_dryrun rejects unknown phase fields and invalid wait/recenter values.
- Dropped rejected S8 CPU job/plan; retained camp/inn m4_generated + GI guard screening.
- Dryrun PASS: 2 INVALID without evidence / 2 structurally valid; scene-wait/capacity lints pass.
- Batch: 499 s phases, <=240 s waits, 18 traces, 1,100 MB; generated stays off, GI gates pending.
- Build + 134/134 CTests PASS; 631 camera-plan checks and QA detectors PASS; diff check PASS.
- No game launch, commit or Git metadata changes. No key, ABI or shader change; variants delta 0.

## Pak material coverage (cov-paks2, READY_FOR_LIVE, 2026-11-03)
- No game, saves, git metadata writes, commit, source deletion or promotion.
- Compact B3PAKS1 v4: 40,983 rows, 6,091 COLOR layouts, 85,630 texture refs.
- Binary 41,956,440 bytes (40.01 MiB), versus 33,386,148,008 bytes (31.09 GiB).
  Full source digest validation/conservation passed; C++ parsed every compact row.
- Additive shared prefix: mesh_paks_cov_paks2_compact_20261103 .bin/.manifest/.json.
  Runtime prefers its complete manifest; 512 MiB/file and 2 GiB/set caps unchanged.
- Retains native content ID/canonical samples, UV signature, counts, UUID, flags,
  texture parameter/UUID refs and explicit canonical COLOR_0 RGBA layout.
  Full ordered geometry/COLOR SHA-256 witnesses replace geometry/color payloads.
- Native content ID reads bound float3 input before VS instance transforms,
  not XFB. Pak IDs can agree with unchanged native upload bytes/index order.
- Expanded XFB has changed positions, implicit corner indices and count/span;
  pool inverse transforms restore approximate positions, not original float bits.
- Candidate join now uses count + ordered UV0 (byte 16 in 64/88-byte XFB).
  Ambiguous contracts reject. Native ID equality/difference remains diagnostic.
- Read-only replay audit: S6 unique 0/1,015 (36 missing counts, 979 UV mismatches);
  camp 58 unique + 4 ambiguous/471; S8 52 unique/602. Full witnesses 0 everywhere.
  Matching-UV examples differ in position by 2.83e-6..6.68e-6 after reconstruction.
- Publication still requires full ordered SHA-256, current resources, shader pair,
  cull and basis. Tileset also requires all native RGBA. No UV-only promotion.
  Device-local UV candidates lack a full witness and remain unqualified.
- Loader/attempt/match/rejection logs distinguish missing, ambiguous, invalid,
  witness, recipe/cull and pending/unsupported inputs. Texture-view bridge absent.
- Five focused CTests pass (including compact conservation/corruption tests and
  synthetic transformed/expanded 64/88-byte layouts); host build passes.
- Camp diagnostic: 75 s + load, ON/OFF/ON + restore, visible_unfocused_fg,
  both requested keys on at startup. Predicted JSON and fake-runner dryrun pass.
  Prepared outside watched inboxes; no live run, visual gain or GPU cost measured.
- ABI and shader request delta 0. Lava dynamic recipes/Ultra parity unqualified.
- Coordinator may delete the 517 full_20261103 files only after accepting compact:
  exact additive inventory in compact JSON; 33,602,532,633 bytes including metadata.
  Other prefixes/caches are excluded. No compact file depends on full files.
- Design/diagnosis: tools/assets/mesh_content_id.md. Full structured evidence:
  docs/progress/cov-paks-compact-report.json; previous provenance reports preserved.

## Core review guards (branch core-review-fixes, offline verified, 2026-11-03)
- Borrowed water lighting has no lifetime provider: refuse F-1 hit shading; retain ordinary compose and same-frame shading. Key-off unchanged.
- Reflection async fallback now requires the installed full/half mode to match requested bindings, dispatch and resolve, including SHaRC/hit variants.
- Full build and 144/144 CTests passed; strengthened lifetime and bidirectional resolution regression tests rebuilt and passed 2/2.
- No shaders or shader includes changed; identity has no touched requests, manifest delta 0, ABI unchanged. No game launch or GPU cost measurement.

## Primary strand hair, increments 1a-1c (offline, 2026-11-03; user: "si facciamolo")
- Design docs/RTX_HAIR_PRIMARY.md: the matched static hair draws run strand variants (pipeline clones):
  prepass without depth writes, G-buffer `shaders/rt/hair_primary_gbuffer.hlsl` (camera ray into this frame's
  groom TLAS, discard on miss, strand depth/normal/tangent, native material FS 42a95e17 at the strand's UV).
- 1a: `hair_primary.hlsli` + twin `tools/hair_groom/primary.py`: depth on the card ray exact (< 1e-9),
  encodings through the game's decoders (normal < 0.2 deg, tangent < 0.1 deg), CPU tracer.
- 1b: GPU check `hair_primary_gpu_check` + CTest `hair_primary_gpu_test` (RTX 5090): synthetic IoU 1.000,
  depth rel. 7e-7, normal 0.0004 deg; Shadowheart (tapered radii) IoU 0.995 (report only).
- 1c: key `rt_hair_groom_primary` (off, live, GUI "Strand hair on screen", needs rt_hair_groom);
  `layer/hair_primary.cpp` (variants, draw swap, header after the groom TLAS), per-point UVs in the groom
  update. Requires LSS, shaderInt64, fragmentStoresAndAtomics. Not run live.
- 2: velocity variant `hair_primary_velocity.hlsl` (native FS b70cadc5 form, last frame's strand points,
  +12 B/point). Cost line `[hair primary] strand G-buffer draws GPU X ms/frame` (per-draw timestamps).
- 3a: fibre BSDF in HLSL (`hair_bsdf.hlsli`), GPU vs CPU max rel. error 1.5e-5; not wired yet.
- 3b-1: dual scattering `tools/hair_groom/dual.py` + path-traced per-channel correction: CHAR_Hair colour 1.01-1.02 of a
  path-traced fibre slab (single scattering 0.21-0.40, plain dual 0.70-0.75); held-out colours 0.97-1.04.
- 3b-2a: `hair_dual.hlsli` (generated tables) = `dual.shade_table` on the GPU (max rel. 1.4e-5). A 3-D static
  const HLSL array read shifted fields on the GPU: flattened (check other multi-dimensional tables).
- 3b-2b: key `rt_hair_fibre` (off, live): `gi/hair_fibre.hlsl` after D-LS lights strand pixels with the fibre BSDF
  (sun + unshadow-mapped local lights). Identity 236/237, CTests, GPU twin 6.7e-6. Not run live.
- Live job READY_FOR_LIVE: tools/live_batches/hair_primary (Dialogue1, cards/strands shots at full resolution,
  cost cards/strands/cards; gated on G-buffer and velocity strand draws > 0). Dryrun pass, not run.
- Fix after the user trial (device lost x2, player bald): no groom update for held/uncaptured draws (pool offset read
  as soup offset), every soup read bounded; card hiding gated on the G-buffer variant having run and on GPU-counted
  strand pixels (2 empty frames: cards back for the session). Tests in hair_groom_asset_test. Not run live.

## Hair reload fail-safe (branch claude/hair-reload-fix, 2026-11-03)
- hv1 18:40 device lost after the F8 reload with strand variants on: native hair FS module map was never pruned on
  vkDestroyShaderModule (a reused handle could give a non-hair pipeline a strand variant); now erased on destroy and
  on reuse. Level loads (new G-buffer depth image) reset the groom gate, pixel record and feedback and pause swaps
  300 frames. Regression test in hair_groom_asset_test (reload sequence). Build, 143/143 CTests. Not run live.
- Join caches: bvhsplit drops groom join verdicts, UV joins and UV readbacks in flight at the same level-load
  boundary (identities built from Vulkan handles a new level reuses). Checked, no change: held hair bounds read
  from the mesh pool at stride 88; groom points come from this frame's soup; held groom update guarded (e43731cc).
- Live jobs: hair_validation hv5-reload-bisect (Test Dialogue1, one launch, visible_unfocused_fg, F8 per stage:
  native + scalp, + physical origin, + groom, + primary, 3 x 20 s survival windows with frames gated in each);
  hv4 adds the reload under GPU-AV. live_flow 5 jobs 0 failing; dryrun passes (31.3 min, 1.49 GB; budget 40 min).
  Build, 143/143 CTests. Not run live.
- Review fixes (main f5b339eb findings): groom_make_room keys residents by an index into (id, LOD) refs (id * 4 + LOD
  lost the top bits of 64-bit hash ids: empty asset, out-of-range LOD on eviction). LOD bounds read from a 4-slot
  readback ring, newest slot of a GPU-completed frame (was the slot this frame's copy may still write). A failed groom
  TLAS build clears kInfoGroomReplaced on its cards (they had lost occlusion and shadow). Strand G-buffer hit path
  checks k < grooms like the miss path. hair_fibre removes the native sun term only for a known visibility (kept on
  kRtUnknown, as local lights). Shader identity: 258/262 byte-identical; only hair_fibre (3) and
  hair_primary_gbuffer differ (key-on shaders). Open: strand ownership per card draw (overlapping grooms) needs a
  per-draw channel to the fragment shader; not a small fix.
- Strand ownership per card (overlapping grooms): the G-buffer and velocity variants keep a hit only when it lies in
  the band of the fragment's own card along its ray (hp_on_card: (10 mm radius bound + segment length / 2) / |cos| +
  pixel spread; strand points lie on their card triangles). The pixel cache is unchanged (closest strand of all
  grooms); the owning card's fragment writes it. Pixel entries sealed (word 3 = hp_seal(tag, words 0..2)): a torn
  entry never pairs this frame's tag with an older key. Python twin + two-groom overlap fixture in hair_groom_test.
  Identity: 256/262 byte-identical; only the key-on hair_fibre (3), hair_primary_gbuffer, _velocity, _count differ.
  Cost: one 4x4 inverse per strand fragment (was per pixel miss); not measured live.

## Hair: every style converted, crowd budget, cost per character (branch claude/hair-batch-lod, 2026-11-03)
- Batch over the mesh_materials v2 catalogue (1217 LOD0/LOD1 CHAR_Hair objects, 4 jobs, BelowNormal, cached per
  style in captures/hair_groom_batch/state.json), 1 h 52 min: 519 pass (written, 13.3 GB), 553 needs review (not
  written: mirrored UVs, coverage, radiance), 145 reject (silhouette IoU, root channel, 22 zero radii in a thinned
  LOD). Pass min-view IoU median 0.94. Details in docs/RTX_HAIR_PRIMARY.md 5a. The 22 radius rejects left 296 B
  header-only stubs in data/assetdb/hair_grooms (asset.write fixed and tested; the stubs await a decision).
- Crowd budget `rt_hair_groom_crowd_ksegments` (off, live; GUI Performance): largest characters first within a
  per-frame segment budget, coarser LODs or cards beyond (`hair_groom::plan_crowd`, unit tests); LOD segment
  counts read from the file headers at index time.
- Cost model `tools.hair_groom.cost` (bench constants, header-only reads: 0.6 s for 549 files): 527 styles, LOD 0
  median 541k segments, 57 MB per instance, 0.06 ms refit; 14-character scenario 0.80 ms, 343 MB.
- Includes main up to bundle7. Live job tools/live_batches/hair_crowd (2 launches, visible_unfocused_fg, 12.6 min,
  1.8 GB): live_flow 0 failing, dryrun pass, not run.

## Continuity and exact GPU merge (branch bundle6-20261103, READY_FOR_LIVE, 2026-11-03)
- Resolved gpu-opt1 into rt-continuity without writing Git metadata or launching the game.
- Kept tileset specialization, compatible pipeline continuity, separate ordinary/hit caches and F-1 query ownership.
- Added exact GPU states to water specialization/cache keys and visible live Experimental prewarming.
- Compact hit state uses SpecId 1024; alpha metadata 1022; empty water tiles 1023. Water hit cost keeps reserved 1021.
- All three candidates stay off in defaults/presets. ABI unchanged; generated shader requests +0 (145 variants, 54 bases).
- Schema and variants regenerated with their C++ writers; defaults check passed.
- Build passed. Full CTest run: 134/135; stale prewarm expectation fixed, targeted rerun passed (all 135 now pass).
- PP diff main: 64 unchanged, 173 changed by specialization code. Sampled key-off equivalence: 63 requests/66 entries passed.
- Water-hit equivalence: 9/9 requests, 96/96 comparisons passed. Positive controls distinguish enabled code.
- Production replay: 9 fixtures x 4 ON states = 36 byte-exact comparisons passed; mixed water 39 owned/26 rays, invalid 39.
- Final diff whitespace and conflict-marker checks passed. Resolved files remain unmerged in the index until staged.
- Live continuity, compose hit attempts, Ultra smoke and GPU cost remain unmeasured; READY_FOR_LIVE requires explicit batch steering.

## Bundle 6 conflict resolution (branch bundle6-20261103, offline verified, 2026-11-03)
- Preserved rt-continuity, gpu-opt1, gpu-opt2 and water-hit-cost. No game launch or Git metadata writes.
- Water pipeline: 11 specialization values and map capacity; all values are in the cache key. Hit cost uses offset 40 (SpecId 1021).
- SpecIds 1016-1024 are unique. Generated schema: 12 user experimental and 12 developer knobs; water hit cost appears in Advanced.
- Regenerated manifest: 163 requests, unchanged from bundle HEAD, +18 versus main (half-trace layout variants).
- watercost: 9/9 requests, 320/320 comparisons; positive control 77,454 differing normalized lines.
- waterhit: 9/9 requests, 96/96 comparisons; positive control 84,679 differing normalized lines.
- Initial gpuopt baseline run was confounded: one sampled RT_TRACE_HALF=1 request compared an enabled layout against pre-layout main.
- Fixed key-off selection to use full-trace counterparts, preserving other defines; added regression coverage (15 tool tests pass).
- Corrected gpuopt: 63 requests/66 entries pass against main; 9 byte-identical before specialization, 57 frozen comparisons pass. Positive control 25,353 lines.
- Main preprocessor comparison: 237 requests; 60 identical inputs, 2 pp-same, 175 pp-different, no preprocessing failures. This is not a byte-identity claim.
- Final build and all 135 CTests pass (109.18 s); defaults, whitespace and conflict-marker checks pass.
- GPU cost and live rendering qualification remain unmeasured; no feature promotion in this merge resolution.

## Reflection half-grid descriptor fix (branch bundle6-20261103, offline verified, 2026-11-03)
- Removed set 0 binding 31: it exceeded D-RF's 32 push descriptors and failed all 12 half-grid hit-shade interfaces.
- Trace and resolve reuse unused GI raw RGBA16F binding 8; only half-grid reflection dispatches bind the dedicated target there.
- Restored the existing 32-descriptor layout; manifest delta +0, ABI unchanged, no game launch or Git metadata writes.
- Checker now requires raw binding 8 only for half-grid traces; direct hit distance binding 30 belongs to their full-grid resolve.
- First compile cleared the binding failure but exposed the checker's stale direct-output requirement; corrected before final validation.
- Full merge-green shader step: 262/262 requests, 298 cached entries, 28 hit-shade interfaces checked, 0 failures; PP self-check 4/4.
- Full-resolution identity against bundle HEAD 6fef43f3461e: 243/243 requests byte-identical (excluding half-only resolve/variants).
- Final build and all 142 CTests pass (161.26 s). Live quality and GPU cost remain unmeasured; no promotion.

## Stage 6 first pass: release package, installer, README, licences (branch worktree-agent-a044365d27d7f43c4, offline, 2026-11-02)
- Release defaults audit (565 `cfg_*` call sites against the package profile): no new diagnostic leak on.
  Pinned `rt_hit_reference=0` in data/release.ini (already 0). Kept on by design and doubtful items: docs/release/RELEASE_BETA.md.
- scripts/package.ps1 fixed: it shipped no common/ headers (21 shader files include them: the RT path could not
  compile in a player package) and no asset DB. Now an explicit list, versioned dist\BG3RTX-<ver>.zip (69 MB) + symbols zip.
- No implicit layer or registry (launcher sets VK_ADD_LAYER_PATH per process): scripts/release/install.ps1 /
  uninstall.ps1 (+ .cmd) copy to %LOCALAPPDATA%\Programs\BG3RTX and fully remove it (shader cache included),
  never follow links, refuse game and Larian folders. Tested in scratch folders (install, update, guard, uninstall).
- docs/release/README.md (players), THIRD_PARTY.md (shipped notice), RELEASE_BETA.md (audit, licences, what remains,
  diagnostic-package design).

## Long sessions: log cap, VRAM line, trend gates, long_session job (same branch, offline, 2026-11-02)
- bg3rtx.log capped at 64 MB (common/log_rotate.h, layer/util.cpp): the full file and the previous session's
  log move to bg3rtx.prev.log; at most two files. CTest log_rotate_test (221 checks).
- New layer line `[mem] device-local VRAM of this process: used / budget / heaps` next to each `[pace]` line
  (VK_EXT_memory_budget, physical-device query only; restart to get it).
- queue_gates.py `"trend": true` gates: tail windows' median <= baseline max + baseline spread + print precision.
  6 new checks in queue_gates_test.
- tools/live_batches/long_session (READY_FOR_LIVE, not run): camp, Ultra + FG 2, 37 min, 22 cycles of 5 cost
  windows + a 10 s rt_stats pool window; scene wait / capacity in all 110 cost windows; trend gates on VRAM, mesh
  pool, AS, frame time median/p99, pool records (cycles 20-22 vs 2-4); predicted.json all flat. Dryrun: all checks passed.
- docs/USER_SETTINGS.md: Ultra levels (RT water + Refraction, + Bounce, Hidden hits, Far-field 6 m), pool alpha
  and fg_letterbox automatic, log cap.
- Build + 96 CTests passed.

## User decisions needed
1. **SHaRC headers:** shipping them is source distribution under the NVIDIA RTX SDKs licence (object code only).
   Not shipped now (sharc=0 everywhere). Before SHaRC ships: precompiled SPIR-V, or ask NVIDIA?
2. **NVIDIA notification:** the RTX licence asks for one before a commercial release. Notify for the free beta anyway?
3. **Larian data:** the asset DB holds numbers extracted from the game's .pak files. Ship it (as now), or check
   Larian's mod terms first?
4. **BG3RTX's own licence:** the repository has none. Which one (it must not put the NVIDIA parts under an
   open-source licence)?
5. **Licence texts:** DXC (NCSA) and Vulkan-Headers texts are not on this PC; OK to fetch them into external\licences\?
- Open: long-session live run (needs steering), Act 2 / most of Act 3, non-5090 GPUs, fresh-PC install test.

## Water flicker at display rate: FG water vectors, glint filter, presented-frame scoring; wf5 job (branch wf5-fg-water, READY_FOR_LIVE, 2026-11-02)
- Diagnosis from the kept logs (wf4 143137): every frame with a water pass was composed in every window, static and
  turn (uncomposed 0, on/off flips 0, no scene / no lighting refusals 0). The compose reads the PREVIOUS frame's
  lighting on 108-120 of 120 frames and this frame's on 0-12, in static and turn windows alike (an intermittent
  one-frame source switch, visible only while the camera moves). The water's sun visibility is one sun-disc sample per
  pixel per frame (rt_shadow's jitter): at shadow edges a ~1000 glint switches on / off from frame to frame. The
  wf2-wf4 metric (pan_artifacts) scored only dips of RENDERED frames: a glint switching ON, and anything DLSS-G adds
  in the generated (presented) frames, was invisible to it.
- Tools: pan_artifacts now also reports peaks (`luma_peak`, the dip rule on -luma; water_mask.peak_rate);
  scripts/qa/fg_present_flicker.py scores presented PNG sequences (second difference per present parity, water mask
  vs rest). Layer: `fg_snap_any` (capture presents with FG off too), `fg_snap_delay` (start N frames after the
  request, inside the turn). Unit tests in tests/pan_artifacts_test.py (20 pass).
- Keys (off, live): `rt_water_fg_mv` (DLSS-G gets the water_mv vectors: the water surface's parallax; double-buffered
  by frame parity), `rt_water_glint_aa` (rt/water_glint.hlsl pre-pass + WATER_GLINT_AA compose: each lit sun record's
  sun term replaced by the (1 2 1)^2 tent mean of the valid neighbours', the mean kept). Screen-space, not the FS
  roughness patch (a SPIR-V rewrite of the FS's roughness uses, not attempted here). No ABI change.
- Job wf5-s8-fg2 (foreground, one launch, 271 s, 5 bursts + 6 x 32 presents): off-a / fgoff (fg=0 live) / fgmv /
  glint / noshadow (rt_water_shadows=0) / off-b, wf3's turn protocol; predicted.json first. Dryrun + lints pass.
- Result wf5-s8-fg2 160335 (INVALID by the presented-capture gate). Rendered K16 bursts (calibrated, water mask), dips /
  peaks: off-a 3.96 / 5.31%, glint 5.06 / 5.52%, noshadow 5.07 / 5.82%, off-b 5.02 / 5.26%; whole frame 4.7-5.2 /
  5.3-5.5% in every burst: the water flashes no more than the rest of the frame in rendered frames. rt_water_glint_aa
  null (+0.09 ms compose GPU, 0.412 -> 0.501), rt_water_shadows=0 null: the 1-spp sun ray is not the cause. Energy:
  glint +1.26%, fgmv +1.32% (band 2.05%): pass. Presented captures: 16-19 of 32 per turn, one present per ~180 ms
  (the readback waited for the GPU and read uncached memory at every present): FG not measurable from this run.
  Fix: the readback finishes at the next present, host-cached memory. Bursts deleted (11.9 GB).
- wf6-s8-fg2 (foreground, 169 s, presented frames only): off-a / fgoff / fgmv / off-b; predicted.json first.
- Result wf6-s8-fg2 162806 (VALID after the gate fix: the every-window select is now the "present capture done" line
  only; re-gated: 8/8 ok). 32 consecutive presents per turn (0.35 s, 0.65 s with FG off). Presented second difference
  (UNORM8 codes), water mask (59k px) / rest, even / odd presents: off-a 3.75/4.16 | 2.49/2.80, fgoff 6.04/6.10 |
  4.00/4.04, fgmv 3.74/4.14 | 2.51/2.82, off-b 3.68/4.21 | 2.51/2.87. Parity ratio water = rest in every turn (1.11 /
  1.12, 1.01 / 1.01, 1.11 / 1.12, 1.14 / 1.14); water / rest 1.47-1.51 with FG on and off alike. DLSS-G adds no
  water-specific alternation; rt_water_fg_mv null (stays off).
- Verdict (wf3-wf6): the port water flicker is not the RT water compose, RR's water inputs, the sun-shadow ray, glint
  aliasing or DLSS-G: the game's animated water changes 1.5x more per present than the rest of the image at either
  display rate. No key fixes it; all water flicker keys stay off.
- wf7 (READY_FOR_LIVE): two launches, wf7a rt=1 (Ultra) and wf7b rt=0 rr=0 (the game's own rendering, DLSS SR),
  same pose and turns, two snap turns of 32 presents each captured 2000 ms into the turn by wall clock (new layer key
  fg_snap_delay_ms: the same pose at either frame rate), wf7a's rt_debug 21 mask from the same start pose and time
  (wf6's mask turn started one turn away from the snaps), one static PNG shot per launch. predicted.json: water / rest
  ~1.5 in both = the game's water, closed.

## rt_water_rr_mv: RR's motion vectors on water; wf4 job (branch wf4-rr-water, READY_FOR_LIVE, 2026-11-02)
- What RR gets on water today: the game's depth (D24S8) and motion vectors (its RG16F velocity pass), both of the BED
  (or the sky over an empty G-buffer pixel): the water writes neither. The guides (rr_guides.hlsl) are built at the
  G-buffer pass, also the bed's. `rr` is live (0: the game's DLSS SR, history reset; ngx.cpp).
- Fix (off): `rt_water_rr_mv` (live). shaders/rt/water_mv.hlsl, after water_primary: our RG16F copy of the game's
  vectors, on water pixels mv + prev_px(P_water) - prev_px(P_bed) with the previous frame's lighting camera (stored by
  the pass; same pixel and one previous camera for both points, so both frames' jitter cancel). ABI 26
  `set_rr_motion_vectors`: the layer gives it to RR instead of the game's (same size and format, while fresh) and puts
  the game's back after the evaluate; DLSS-G keeps the game's. Depth and normal/roughness guides unchanged (next, if
  the motion helps). ABI 25 -> 26: rebuild both DLLs, restart the game.
- Identity: no existing shader changed (rt/water 33/33 identical to main by inputs); water_mv compiles, passes
  spirv-val, its bindings match the layout. Build + 97 CTests pass. No unit test: the logic is the shader's.
- Job wf4-s8-fg2 (foreground, 173 s, 5 bursts ~11.5 GB): rot-off-a / rot-rrwater / rot-rroff (rr=0) / rot-off-b,
  wf3's turn protocol and masks. predicted.json: energy null, cost +0.02 ms; flicker below both OFFs = RR's water
  reprojection. wf3's job removed (analysed). Dryrun + lints pass.

## OMM option B: fine-LOD key, mip-0 coverage, om3 job (branch omm-option-b, READY_FOR_LIVE, 2026-11-02)
- Key `rt_alpha_fine_lod` (live, off): masked VT records (rt_vt_alpha_masks) bind a mip-0-only view, so the RT
  alpha test reads mip 0 with the hard cut for every ray. No shader change (key-off SPIR-V untouched).
- Derived energy: grass alpha coverage 0.262 -> 0.295 (x1.125, darker); measured by omm_qual om3 (camp, 123 s).
- Micromap coverage at mip 0 (camp pack): level 3 34% known (19% opaque, 15% transparent), level 4 49%,
  level 5 62% of micro-triangles.
- Not done: the bake. Camp grass is 179 per-draw BLAS (0 pool meshes); plan in docs/P4_OMM.md 11.2c (about 2 days).

## Fourth instanced family: IP grass at camp (branch hm-camp-grass, READY_FOR_LIVE, 2026-11-02)
- Camp's largest hit-material blocker (m4p1 031746: row reason 60% of lookups) is IP_Wind_GrassFlowers_AlphaTest_2S_VT /
  _PBR_VT: depth 05d39f75/73849471, G-buffer 105e47e4/35a18400 (asset DB). The G-buffer FS is the Base_VT decode
  ("same sampling", tests/material_families_test.py IpGrass) with its VT layers at 1:4..1:7 (`LayoutVt4`), engine UBO as
  the 2S family (index 44, channel 48, flag 52), reflectance at 1:1 byte 16, plus a camera-distance dither discard.
  Both VSs place the vertex with textually the same wind arithmetic: rows checked bit for bit, no tolerance.
- Key `rt_hit_material_ip_grass=0` (startup: the 105e47e4 frame patch at N location 2 and the RT_HIT_IP_GRASS variants;
  then live 0/1 of its rows; needs rt_hit_material_instanced). Family 4 in word 220 bits 8..10: a consumer without the
  define reads bits 8..9 = 0 and checks the row bit for bit with the plain rule (fail closed, uncounted); with it the
  3-bit field and clip counters 435..442 ("hit material instanced clip IP grass"). Pair check as wind plus the 2S
  translation; remap counters per family (perm base 64 -> 80).
- S6 lava (FLUID_Lava_Plane_Flowmap bd5dbda2/0b8dfd73): no small recipe. The FS animates a flowmap with frame time and
  reads a per-vertex colour, the absolute world position and 22 material floats over 4 textures; the pool row and
  basis carry none of them. Its look is mostly emission, which hidden hits already add. Not started.
- Live job `tools/live_batches/m4_ip_grass` (camp, 4.4 min, 0.2 GB), dry run passed; predicted qualified +30 points
  (10-40), shot-on null (band 3.5%).

## Release product review (branch codex-product-review, design ready, 2026-11-02)

- Added `docs/release/PRODUCT_REVIEW.md`: source inventory, ranked findings, original branding,
  per-user Inno setup design, launcher layout, diagnostics privacy, first run and error states.
- Current UI is the Win32 launcher; own in-game settings overlay and Diagnostics are planned only.
  Current table has 8 player Experimental knobs, versus 4 in USER_SETTINGS and 5 in progress.
- Release risks: partial upgrade recovery, package/data provisioning disagreement, missing required
  notices, saved versus active state, choice-level experimental/developer visibility.
- Three future implementation streams have exclusive ownership and acceptance checks.
- Docs only. No builds, CMake, CTest, DXC, game, installer or launcher execution. No Git writes.
  No saves accessed. Shader request delta 0; no runtime/GPU cost change.
- Visual inspection remains pending: captures/ is absent in this worktree. Review UI screenshots
  and disposable install/report fixtures when implementing; rendering lives need separate steering.

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
