# BG3RTX progress - 2026-11-05 (consolidation)

## Report: optimisation night 2026-11-05 (main, release candidate rc-20261105-2aa5dcf8 PASS 12/12)

**Question for the user (only one): pubblichiamo la 0.9.1?** Files: `.scratch/rc-test/rc-20261105-2aa5dcf8-30de6495/`
(Setup.zip, Setup.exe, portable ZIP). Nothing uploaded.

**Qualified, always on** (in-run A/B at the inn, 600-frame windows, OFF repeats as noise):
- `rt_scene_compact_holds`: High scene build -0.87 ms GPU (OFF spread 0.075); energy within OFF spread; BVH identical by fixtures.
- `cpu_opt1`: layer hook bodies -0.13 ms/frame CPU (spread 0.016); BVH build -0.2 ms.
- `cpu_opt2`: -0.30 ms/frame CPU (spread 0.088). `cpu_opt3`: -0.24 ms/frame CPU (spread 0.054).
- Total CPU about -0.67 ms/frame at the inn on Ultra (layer hooks 12.4 -> about 11.7 ms/frame).

**Before/after on the release build (static, ms GPU, profile runs 00:13/00:20 vs 06:51/06:55, all VALID):**
| Scene | Preset | Scene build | GI | Local shadows | RR |
|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.19 -> 1.18 | 12.20 -> 12.10 | 0.69 -> 0.59 | 2.60 -> 2.53 |
| Water Queens House | High | 1.92 -> 1.27 | 2.06 -> 2.24 | 0.13 -> 0.12 | 2.48 -> 2.48 |
| Inn | Ultra | 2.45 -> 2.49 | 21.27 -> 21.18 | 2.03 -> 2.01 | 2.53 -> 2.54 |
| Inn | High | 3.42 -> 2.14 | 3.09 -> 3.07 | 1.07 -> 0.87 | 2.61 -> 2.57 |
Single static windows; differences under about 0.2 ms are within run-to-run noise. Camp and Dialogue1 were profiled
(OPTIMIZATION doc) but not re-run after the changes. Stock-hair final smoke VALID (124 s).

**Rejected with numbers** (code removed): gi_roof_reject (inn GI 21.75/21.58 OFF vs 21.87/21.95 ON), rt_origin_lazy_normal
(no saving), cpu_opt4 (+0.47/+0.54 ms CPU, slower). Dropped offline: GI temporal current-sample, reflection/local-shadow
early-outs (no exact proof or under 0.05 ms); ray flags already first-hit. Rounds stopped when a round found no win.

**Not reached:** Ultra GI trace (8-13.5 ms) stays the largest GPU cost; per-feature attribution needs a live tool that sees
cached pipelines (post-release). The final Ultra+High S8 release-smoke job is refused by its own trace-window preflight
(tooling); the profile runs above served as the smoke. Next per user: bug fixing.

Details: [OPTIMIZATION_20261105](OPTIMIZATION_20261105.md), [RC_CHECK](release/RC_CHECK.md).

## Report: project consolidated offline, final qualification ready

**Final qualification live (user go, 2026-11-04 23:24-23:38, main 66ed523b, release candidate rc-20261104-66ed523b PASS 12/12).**
- Stock hair, Dialogue1: VALID, 123 s. Float energy +0.23% vs 21:04 (0.37525 vs 0.37439). One-sample costs vs rel3:
  RT +0.57 ms, GI +0.45 ms, GI trace +1.0 ms (9.84 ms): single windows, not a measured regression.
- Ultra, Water Queens House: every Ultra window gate passed in two launches (statics, pans, rotations, Ctrl+Shift+F9
  A/B); 0 device lost, 0 deferred destroys. GI 11.3-12.2 ms in measured windows (11.46 ms at 17:52), RR 2.38-2.40 ms,
  post temporal 0.14 ms. First launch timed out on the cold shader cache (50 compiles after today's shader changes).
- High part not completed: its readiness waits were generated wrong (rt_build=auto printed as 1; a box-light
  pipeline that High never builds). Test-job defects, not product faults; the alpha/OMM path High uses is the same
  as Ultra's. No further reruns (lean rule). Post-release: fix release_qual/make_final.py readiness for presets.
- Found by the user before the live and fixed: the package lacked the runtime alpha table (all foliage was opaque in
  ray tracing, "cardboard" tree shadows); release logging was verbose (now quiet, Detailed logging switch).

**Released: BG3RTX 0.9.0 beta** (user approval 2026-11-04 after their own test: vegetation shadows fixed). Git tag
`v0.9.0-beta` on 66ed523b. Files for Nexus (upload by the user): `.scratch/release/v0.9.0-beta/`
BG3RTX-0.9.0-beta-Setup.zip (main file), BG3RTX-0.9.0-beta-Portable.zip (SHA256 b479fda0..., same as the RC check),
SHA256SUMS.txt. Next: overnight optimisation goal (no visible quality loss), Codex-driven.

**Done (all on main, pushed).**
- Five independent reviews (layer; core, shaders, common; product surface; shaders; tools). Every finding
  confirmed in the code first; table with the outcome of every row: [CONSOLIDATION_20261105](CONSOLIDATION_20261105.md).
- Fixed: 19 layer findings (resource lifetime kept until GPU completion, per-queue pruning, FG state only on
  SDK success, init/bind/recording failures unwound, asset DB bounds, latches), 8 core, 15 product (launcher
  failure paths, rejected options out of the opt-in, ZIP install rollback), 5 shader (rr_post overflow,
  alpha mip clamp, UV edge loads, reprojection validity, half-float clamps), 20 tooling (shader identity trusts
  only intact blobs, QA gates fail on missing data, deterministic asset DB, about 170 unregistered tests).
- Not confirmed, written with the reason: C4, C7, C8, C12 (rings larger than frames in flight).
  Deferred with the reason: L11, L12, L17 (need submission-dependency tracking), S-F05, S-F02, T2.
- Cleanup: 8 of 9 rejected options removed (pak publication, generated rows, IP grass, alpha spill, water
  hit shading and cost bits, half resolution, current sun, GI guards, 3 GPU candidates). SHaRC stays: its
  device capability also serves water and hair. Shader requests 167 -> 122; about 4,000 source lines removed.
  Release path proven equivalent with frozen specialization constants (71 identical, 139 equivalent, hair
  requests identical against main, control differs). Last code with them: tag `archive-20261105/pre-cleanup`.
- UI/UX (Opus review, 26 changes, before/after screenshots at 100-200%): plain help texts with restart notes,
  Experimental shows only Water surface reflections (strand hair hidden until its update, user decision),
  preset review, Help & support, first-run folder notice, installer texts.
- Installer: Inno Setup 6.7.3 pinned by hash, same payload as the ZIP; status text while preparing;
  lifecycle install 63 -> 15 s. Payload 229 -> 219 files (removed shaders).
- Release check PASS 12/12 on the candidate ([RC_CHECK](release/RC_CHECK.md)): build, 219 files, hashes,
  licences, ZIP install/update/uninstall, setup install/update/uninstall and Apps entry.
- Tests: 172+ CTests green at every merge; merge-green now lints only active or new live jobs.
- Nexus kit (`docs/release/nexus/`): title, short and main description (the published version plus intro,
  Beware of Dragons, NVIDIA RTX + Vulkan only), sticky post, header 1300 x 372, graphics script.
- Lives today (user go): rel1 Ultra S8 VALID 166 s, rel3 stock hair VALID 127 s on main ce1448bf
  (after the layer fixes): no deferred destroys, no device lost, RR temporal 0.12-0.13 ms.

**Open, not claimed.** Live confirmation of the cleanup, shader fixes and UI on the release build (the final
qualification); S8 float energy has no historical baseline (reported as unavailable, not pass); signing;
fresh-PC install; Act 2/3 acceptance (user).

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).

## Optimisation analysis and variant warmup (branch opt-tools2, offline verified, 2026-11-05)
- Fixed both A/B analyzers through shared `ab_receipts.gpu_samples`: the reset-aligned reader returns text, not timestamp pairs. Regression fixtures exercise the real reader for both producer shapes.
- Accept the captured rr_post_ac/rr_post_eb diagnostics without weakening required K8 transport; resize the animation mask from G-buffer to final-image resolution. Missing/nonfinite required inputs still fail closed.
- Reports: `.scratch/opt-tools2/scene.scene.json` and `origin.origin.json`; historical jobs/plans retained in `.scratch/opt-tools2/evidence`. Original evidence is read only; no new live run.
- Scene 014837 convert medians OFF a/b/c 1.9780/2.0445/1.9695 ms, ON 1.1270 ms; OFF spread 0.0750 ms, ON minus OFF mean -0.8703 ms. Scene build delta -0.9158 ms.
- Scene pre-post float luminance OFF a/b/c 0.09224510/0.09201208/0.09312516, ON 0.09224315; delta -0.00021764 versus OFF spread 0.00111308 (0.196 spreads). RGB and source dumps.json per window are in the report.
- Origin 012845 medians OFF a/b 1.514/1.495 ms, ON a/b 1.634/1.520 ms; OFF spread 0.019 ms, ON deltas +0.1295/+0.0155 ms: no qualifying saving.
- Origin float luminance OFF a/b 0.09173112/0.09046369, ON a/b 0.09038009/0.09007743; deltas -0.00071732/-0.00101998 versus OFF spread 0.00126742 (0.566/0.805 spreads).
- Both reports are valid FAIL receipts: existing QA flicker detector fails all ON/OFF pairs; energy stays within the derived bands. Neither candidate is qualified. Static rendered evidence only; no FPS inference.
- Prewarm gi_roof_reject (1028) and rt_origin_lazy_normal (1027) in dev/detailed logging, or explicit opt_prewarm job mask (1 roof, 2 origin, 3 both). Ordinary release startup adds neither candidate; compact holds is layer-only. No shader or ABI change, shader request delta 0.
- GI attribution warms each of 12 feature states before cost windows with selected-pipeline and fresh driver-quiescence witnesses; 240 s total warm readiness, 504 s watchdog. Roof/origin regenerated too; all three lints have zero problems. Snapshot adds four queries, preserving all prior receipts.
- No commits, game launches or queue staging. Future live confirmation remains READY_FOR_LIVE and needs separate steering; no GPU cost of this tooling change was measured.
- Validation: build succeeded; full `scripts/build.ps1 -Test` passed 177/178, with the water shader gate blocked by shared-cache SQLite access. Retried that gate with supported BG3RTX_SHADER_CACHE in this worktree: PASS (22.3 s). All 178 tests passed across full run and targeted retry; analyzer regressions 15/15, edge QA 16/16, batch lint 12/12, settings check and diff whitespace check pass. Removed oversized generated build outputs; retained artifacts are below 20 MB.

## Scene hold transfer packing (branch opt-scene, READY_FOR_LIVE, 2026-11-05)

- New developer-only `rt_scene_compact_holds=0` independently enables the existing hold-pack kernel when the device captured 88-byte vertices, even when High disables `rt_hair_pool_trim`. Release policy forces it off; no preset or shader request changes. It does not change hair-slot retirement.
- OFF preserves the original predicate. ON still requires static camera, enabled holds and a native capture layout. Existing `dyn_hold_stride` packs only opaque or alpha recipe kinds 1-3 to 24 bytes; native Hair/Tileset/unknown recipes retain 88 bytes. Held opaque instances have no hit-material metadata. Position/w/UV are copied as integer words; BVH xyz and alpha UV bytes are identical. Pipeline failure falls back to the original raw copy.
- `dyn_hold_test` extends the existing fixture with every gate combination, 1/3/63/64/65/129 vertices, NaN/Inf/signed-zero bit patterns, recipe classification, and timing-epoch isolation. Existing hold age, submission and deferred retirement rules are unchanged. Timing-mask bit 4 rejects delayed pre-toggle packing samples without changing the previous four bits.
- Expected saving 0.05-0.30 ms in High inn on an Ultra-provisioned 88-byte device; unmeasured. Inn High copies 210.6 MB/frame against Ultra's approximately 57.5 MB packed plus raw. Standalone High on a 24-byte device has no opportunity. Prepared job: `tools/live_batches/opt_r1/scene_holds/jobs/opt-scene-compact-holds-inn.json` (final lint/build evidence is recorded by the coordinator).

- Final offline validation: `scripts/build.ps1 -Test` PASS 175/175 (175.17 s); late-registered `opt_scene_jobs_test` PASS 1/1 (12 underlying analyzer tests). Local shader cache used because the shared cache was not writable; initial failed attempt discarded. Live-flow lint: 0 issues, 277.72 s including 120 s load. Shader request delta 0; no shader or ABI changes. READY_FOR_LIVE is pending cost/energy/flicker evidence, not a quality or saving claim. No game launch, staging or commit.
- File-size constraint remains unmet for 13 ignored build-generated debug/SDK copies above 20 MB under local `bin`; all changed source/job files are below 20 MB. Automatic approval policy rejected both bounded cleanup and literal file removal ("blocked by policy"); no bypass attempted. Final lint rerun: 0 errors, 157.72 s after load; new-transition readiness medians remain unproven and waits fail fast.

## Scene cost diagnosis and rejected reuse (branch opt-scene, offline, 2026-11-05)

- Profile is not a matched-camera preset comparison: Ultra pan precedes High static without restoring the initial camera. In S8, Ultra static has 282 captured draws / 1.295 M triangles versus High 343 / 1.769 M. Do not attribute the whole difference to a preset.
- S8 static receipts: Ultra convert/extract 0.600, BLAS 0.376, TLAS 0.203, total 1.185 ms (log 5590); High 1.160 / 0.425 / 0.327, total 1.921 ms (7537). Component medians do not add to total medians. Logs are in the main checkout `captures/queue/logs/opt-profile-s8.1.000944.log`.
- Inn (highest static scene cost): Ultra convert/extract 1.208 / BLAS 0.781 / TLAS 0.325, total 2.451 ms (log 6910); High 2.073 / 0.876 / 0.328, total 3.389 ms (9662). Hold transfers are approximately 57.5 versus 210.6 MB/frame (Ultra packed+raw versus High raw); profiles are descriptive, not matched-camera causal estimates. Source: `opt-profile-inn.1.001709.log`.
- High retains the initial device's stride 88 (log 103); `rt_hair_pool_trim=0` disables packing (`layer/bvhsplit.cpp:2149`), so holds copy whole native records. High also rebuilds every 60 frames versus Ultra 600; S8 refits/rebuilds are 336.9/6.1 versus 281.6/0.4 per build. TLAS refit 46% versus 71%. These are observed work differences, not isolated causal timings.
- Dynamic OMM is enabled in both. S8 steady windows bake 0 micro-maps, 0 changed UV triangles; High checks 10.97 M versus Ultra 8.64 M triangles per 120 builds. This rules out repeated baking as the steady cost cause, not UV-check cost. Inn High similarly bakes 0 (log 9661) with 106.55 M UV checks. Timers do not isolate UV checks from packing/extraction. High disables cascade capture, so extra cascade work cannot explain its higher cost.
- Candidate #6 rejected: cascade signature (`layer/geocapture.cpp:1385`) hashes draw identities/counts, not source contents, transforms or shader time. Static classification (`:1155`) checks affine layout and buffer usage only. Buffer-write eviction (`layer/bvhsplit.cpp:3889`) covers mesh ranges, not every cascade dependency; the design explicitly uses later audits for unhooked host writes. No existing complete immutability proof covers a rigid cascade subset, so exact current-geometry replay cannot be established cheaply. No reuse key/job retained.
- Refit/rebuild policy unchanged: per-draw BLAS and TLAS already refit with submitted compatible state (`layer/bvhsplit.cpp:2575,3452`); cascades refit stable list/count up to 60 (`:2916`). Longer unconditional intervals have no demonstrated build-plus-trace win. Earlier FAST_TRACE increased build/refit cost without useful trace improvement (TRACK_D_SCENE_BUILD section 6.5). No new tuning candidate retained.

## Small extraction batching (branch opt-scene, REJECTED, 2026-11-05)

- Candidate #5 already binds descriptors once and shares a trailing barrier (`layer/bvhsplit.cpp:3187-3224`). Settled camp has no extraction/check/audit jobs (log 2198,2391,5009,5212,5408): saving 0 ms there. Inn churn can benefit, but job sizes and isolated extraction cost do not establish at least 0.05 ms.
- Original-soup extraction/status/cache-publication replay is unavailable: `tools/frame_replay.cpp:902-994` starts from extracted geometry. No exact ON proof of the proposed batching and failed-submit retirement contract; reject rather than retain an unproven implementation. No key or live job; shader request delta 0.

## Coalesced XFB conversion (branch opt-scene, REJECTED, 2026-11-05)

- Candidate #3 mapped adjacent subgroup lanes to the three triangle corners, retaining every vertex expression and invalid-triangle collapse. The existing windowless Vulkan fixture compared the full output buffers, including material payload and guard bytes: 680 / 680 exact, 0 validation errors on RTX 5090. Coverage: perspective/orthographic, strides 16/24/64/88/112, NaN/Inf/sky/mixed invalids, singular and nonbinary matrices, partial waves, multi-group and grid-stride iterations.
- Required OFF gate `tools/release_shader_equivalence.py --baseline main` failed both conversion modules: optimized OFF retained unused `OpCapability GroupNonUniformShuffle` despite capability trimming. The retained cascade negative control passed. ON equality does not override the required OFF gate; no equivalence normalization was weakened.
- Dropped the implementation, key, registry entry, fixture changes and live job. No shader request delta or measured saving. The 0.05-0.30 ms combined scene/cascade estimate remains unproven; a shared-memory alternative has no confident saving above 0.05 ms after its barriers. Small local evidence: `.scratch/xfb-release-equivalence.json` and `.scratch/xfb-rejected-proof.txt`.

## Reflection and local-shadow optimization round 1 (branch opt-rflsh, rejected, 2026-11-05)

- No candidate retained. Production code/settings/ABI unchanged; request delta 0.
  No commit, game launch or queue submission.
- Reviewed tonight's four `opt-profile-*.log.profile.md` tables. Inn is the static Ultra
  maximum for both passes: reflections 2.115 ms (2.110-2.119, 2 periods), local shadows
  2.028 ms (2.027-2.029, 2 periods). These are baseline costs, not optimization results.
- Candidate #2, receiver/probe early-out: dropped because the required ON output proof
  is unavailable. `common/replay_pack.h` kPasses only supports gi_hit_direct and the
  water compose fixture; lighting inputs alone do not make frame_replay a reflection
  replay. `rt_reflections.hlsl:433-437,486-495` still evaluates zero-weight arithmetic, clamps
  specular and applies a vector-wide finite fallback. Skipping ambient evaluation
  must establish the same result for NaN/Inf/overflow, negative specular and every
  model/roughness endpoint; w == 0 alone is insufficient. Roughness rejection already
  prevents rays at :232-234. Audit estimate 0.05-0.25 ms remains unverified; retained
  saving 0 ms. No key or live job retained for a candidate without its offline proof.
- Candidate #8, identical visibility reuse: dropped for insufficient evidence of a
  saving above 0.05 ms. Inn log configuration is K1 + one remainder, confirmed active.
  `common/ls_exact_k.h:lk_add/lk_end` partitions list entries into disjoint exact and
  remainder sets. With unique light IDs, these two slots cannot repeat a query.
  Duplicate light IDs across different entries are possible but not counted in these
  logs; zero duplication in the captured game frame is NOT claimed. For two remainder
  samples, `rt_local_shadows.hlsl:1337` changes q == 5's stochastic key, so matching IDs
  alone cannot justify reuse under soft shadows. No speculative cache/register cost
  added. Audit estimate 0.01-0.15 ms only applies to duplicate-heavy configurations;
  no supported positive estimate for tonight's profile. No key or live job retained.
- Other early-outs: `ls_light_traced` already rejects range/channel/cone/normal failures;
  exact-k rejects zero/non-finite contribution (:1322). Empty lists reach `ls_finish`;
  its AO/direct-sun giveback, clamps and finite fallback can matter. A sun shadow cannot prove local lights
  contribute zero. No safe additional zero-output shortcut demonstrated.
- Ray flags: binary `rt_visibility` already uses ACCEPT_FIRST_HIT_AND_END_SEARCH
  (`rt_common.hlsli:1133-1139`); these are inline queries without a closest-hit shader
  invocation to skip. Reflection nearest-hit queries must retain nearest-hit semantics.
- Validation: `scripts/build.ps1 -Test` built successfully; 174/175 passed. The water
  shader gate could not open the shared SQLite cache under workspace permissions;
  its sole retry passed (0.79 s) using a local copy of the water cache blobs/index.
  All 175 tests passed across these runs. Used /Z7 and /DEBUG:NONE /INCREMENTAL:NO.
  Removed oversized generated SDK copies/link intermediates; no file > 20 MB retained.
  Equivalence/job lint inapplicable (no shader edit/job); no energy/flicker pass claimed.
- HEAD/main identical (`rev-list` 0/0); `git merge main` denied at shared ORIG_HEAD lock. No refs changed.

## Visibility ray flags audit (branch opt-rayflags, REJECTED, 2026-11-05)

- Audited all RayQuery / TraceRayInline sites in shaders/gi and shaders/rt at f3d3c41a (also main).
- No safe release visibility change: every binary scene query already accepts the first accepted hit.
- Full site inventory, caller bounds and exclusions: docs/RAYFLAGS_AUDIT_20261105.md.
- Closest-hit consumers need distance/material; occluder statistics need the nearest blocker.
- Alpha candidate resolution and candidate-cap/unknown handling remain unchanged.
- Shader request delta: 0. No key, prewarm, live job, queue submission or game launch.
- GPU cost: not measured; no implementation candidate. No commit (coordinator owns commits).
- Validation: scripts/build.ps1 -Test built successfully; 177/178 CTests passed. Water shader gate
  could not open the shared SQLite cache; isolated retry with BG3RTX_SHADER_CACHE under
  .scratch/rayflags-shader-cache passed 1/1 in 25.84 s. All 178 tests passed across these runs.
- Rendering sources match main exactly (git diff --exit-code main -- shaders common core layer bg3rtx.ini).

## Quiet-only pipeline readiness (branch opt-quiet, kept offline, 2026-11-05)

- Shader-changing keys now require only a fresh QUIET watch record after settings acknowledgement. No generated family/request completion guesses; layer-only and push-constant keys still need no pipeline wait. Unknown keys still require an explicit contract.
- Regenerated gi_roof, scene_holds, gi_attr, release_qual/final and opt_profile through their generators, including release intermediates. The final High diagnostic transition also uses fresh QUIET instead of an inherited generation witness.
- All 9 requested jobs pass live_flow frozen lint. Added 38 missing readiness queries only; every prior snapshot record, regex match and timing model is preserved. New queries remain explicitly unproven.
- Tests: batch_jobs_lint 12/12, live_flow 72/72, release_qual 5/5, merge_scope 9/9. Readiness assertions require QUIET only and reject missing freshness.
- Shader request delta 0; no rendering change or GPU cost claim. No game launch, queue staging or commit. Live confirmation remains pending explicit user steering; the existing inn A/B jobs should proceed past shader transitions without guessed-request timeouts.

## Optimization profiling batch (branch opt-profile, READY_FOR_LIVE, 2026-11-05)

- Prepared four log-only launches: S8, Campsite - 27h, The Blushing Mermaid - 96h 39m,
  Test Dialogue1. Each runs release Ultra then High, detailed logging throughout.
- Queue source folder: `tools/live_batches/opt_profile/jobs/`.
  Exact glob: `tools/live_batches/opt_profile/jobs/opt-profile-*.json` (four jobs).
- Expected durations including 120 s load: S8 3.26, camp 4.54, inn 4.58, Dialogue1 4.70 min;
  total 17.1 min. Hard timeout 300 s each, total cap 20 min. Readiness delays unproven.
- Per preset: static + slow pan, 360 frames each, reset at motion-ready, 120-frame
  cadence, one acknowledged period, at least two complete cost observations required.
  No traces/captures/diagnostics. Mode visible_unfocused_fg. Shader request delta 0.
- Resolved rt_build before readiness comparisons; disabled box/exact-k/temporal/water
  pipeline waits omitted using target state. Regenerated release_qual/final receipts.
- Analyzer emits per-pass Markdown for docs/OPTIMIZATION_20261105.md; shared RR denoise
  explicitly identified, watch CPU/driver totals retain interval units. Drops FG straddle.
- Frozen preflight: 4 profiling + 5 release jobs pass. Existing 79-log snapshot retained;
  15 required readiness queries appended without a full-log cache. 88 tests pass;
  frozen regeneration has no JSON hash drift. CPU timing requires the diagnostics-on test build.
- No runtime costs measured; no speedup/quality claim. Coordinator commits. No launch,
  pending-inbox writes or files over 20 MB. main already ancestor of HEAD; git merge main
  could not update ORIG_HEAD under the workspace permissions, so history is unchanged.

## Exact deferred origin shading normal (branch opt-origin, READY_FOR_LIVE, 2026-11-05)

- Added `rt_origin_lazy_normal`, specialization 1027, default 0 in every preset;
  developer Performance control. No ABI change; shader request delta 0.
- Saves one shading-normal fetch/decode/transform for each finite nondegenerate
  origin with statistics off. No ray, output, tolerance or counter is removed.
- Full resolution: 6,144,000 pixels / 96,000 groups at 3840 x 1600; one short
  alpha-tested primary query per positive depth, plus optional hair receiver work.
  Cascade changes the ray mask only. No safe GI-grid ray skipping or measured
  occupancy improvement was found; groups remain 8 x 8.
- Inn log read: origin 1.182 ms/frame over 28 mixed Ultra/High receipts.
  Candidate GPU cost is unmeasured; comparisons must use the same-run OFFs.
- OFF proof: `release_shader_equivalence.py --baseline main --freeze-spec 1027
  --filter gi/rt_origin.hlsl`, all 3 affected requests equivalent, 17 assignments,
  3 universal certificates, negative control PASS. Other shader sources unchanged.
- ON decision fixture: 10 named cases + 16,384 edge states; output float32 bytes
  preserved, including signed zero and nonfinite/degenerate fallbacks.
  Compiled native inn ON fetches the normal conditionally after traversal;
  OFF fetch dominates traversal. Three static ray initialization sites unchanged.
- Inn draft: `tools/live_batches/opt_r2/origin/`, OFF/ON/OFF/ON with both pipelines
  warmed, detailed logging, separate cost, float pre-post and rendered K8 QA.
  Frozen lint PASS, 0 failures; only 7 additive unproven readiness snapshot entries.
  145.684 s after load, 300 s watchdog; no live measurement or quality pass claimed.
- In game after authorization: inspect static inn surfaces and shadow stability;
  analyzer requires both ON cost savings beyond OFF-repeat spread, energy within
  OFF-derived noise, and temporal QA versus both OFF bursts. No manual toggling.
- `scripts/build.ps1 -Test`: 176/177 passed initially; fixed the new knob's Low
  dependency and regenerated settings schema, rebuilt, then reran the failed
  test through `-Test -TestRegex '^settings_test$'`: PASS (4,129 checks).
- Standard build briefly created oversized PDB/ILK and SDK copies; removed these
  exact generated files. Final workspace scan found no files over 20 MB.
  No commit, game launch or queue staging. Merge-main refresh was denied by
  sandbox ORIG_HEAD permissions; HEAD already equals main (26b06649).

## GI optimisation round 1 (branch opt-gi, candidates rejected, 2026-11-05)

- Cost attribution: the enclosing GI timer includes pre work (origin, trace, temporal,
  AO fold, sun) and post work (local shadows, reflections, composite, water and optional
  diagnostics). Hit direct/sun/emission/far bounce run inside the trace dispatch.
  The reported 4.3-7.8 ms difference is not an isolated GI filtering measurement;
  do not subtract independently aggregated summaries to assign pass costs.
- Added detailed_logging-only GPU timers: origin, temporal, AO fold, sun trace,
  sun resolve, contact and composite. Means per successfully dispatched frame;
  transitions/barriers excluded, asynchronous readback, reset at rt_stats_reset.
  GPU ms remain UNMEASURED. Rendering unchanged; shader request delta 0.
- Roof early rejection: dropped because all five available production replay packs
  use gi_hit_direct without emission set 4 or the temporal/composite chain. Thus
  complete Ultra ON output equality cannot be proved. Audit estimate 0.30-1.50 ms
  remains unverified, not a saving. No retained key or live job.
- Temporal current-sample path: dropped because no production temporal replay fixture
  covers stored formats, history/depth/motion and downstream AO/composite. Cached
  SPIR-V uses FMix; algebra is insufficient proof. Estimated 0.03-0.12 ms is unverified
  and does not establish the 0.05 ms minimum. No retained key or live job.
- Other hit skips: zero local-light terms/back-facing geometry already skip visibility
  (native_hit_direct.hlsli:214,218); sun guards precede visibility (:73-75);
  AO is contribution-gated. Emission traces a separate TLAS, so the ordinary material
  cannot safely identify a zero-emission interval. No safe additional candidate.
- Cached Ultra sun/emission module: 71,319 SPIR-V instructions, 1,230,984 bytes;
  sun-only 69,102 instructions. No driver spill/register/occupancy conclusion.
- Validation: gpu_logging_gate_test 5/5 passed; final core Release rebuild and
  focused CTest passed (1/1).
  Full build is incompatible with the 20 MB cap (SDK copies 48-59 MB); core-only
  build -Test attempted all 175 CTests: FAILED (exit 8), with missing unbuilt
  executable fixtures. No full-suite pass claimed. No shader changes, so
  release_shader_equivalence and settings regeneration are unnecessary.
- No commit, game launch, staged queue job or live evidence. To inspect the new costs,
  use detailed_logging=1 and the existing stats period/reset controls in a separately
  authorised profile; no optimisation A/B is ready from this branch.

## Optimisation night close-out (branch opt-final, complete, 2026-11-05)

- Removed rejected cpu_opt4 code, resource/descriptor receipt tracking, key,
  NO_PIPELINE entry, opt_r5 job and three candidate tests. Retained frozen lint
  receipts. Evidence remains in TRACK_D_SCENE_BUILD and the historical CPU4 note:
  inn Ultra hook bodies +0.47 / +0.54 ms/frame vs OFF spread 0.104 ms.
- Retained qualified cpu_opt1/2/3 and rt_scene_compact_holds, all default 1;
  aligned cpu_opt1's hot-settings fallback with its canonical default.
- OPTIMIZATION_20261105 now opens with every night candidate's outcome and the
  tooling fixes: quiet readiness, layer-only keys, shared A/B analysis, prewarm.
- Inn Ultra CPU receipts total about -0.67 ms/frame (12.4 -> about 11.7);
  High inn scene build -0.87 ms GPU, energy within OFF spread. Other scenes
  were not A/B measured per candidate; no combined-preset measurement claimed.
- Check in game: inn save with the qualified defaults; developer A/B keys remain.
  No new game launch or queue staging. No commit. ABI unchanged, shader requests +0.
- Validation: scripts/build.ps1 -Test built successfully; 181/183 passed
  (283.05 s). Fixed the CPU4 fragment length; shared-cache SQLite write failed
  under sandbox restrictions. Copied the index and water blobs read only into
  .scratch/shader-cache; BG3RTX_SHADER_CACHE selects that writable local cache.
- Final incremental build passed (3 s); both failed tests plus CPU1/2/3 and
  settings checks passed 9/9 (1.83 s). All 183 suite tests passed across runs.
  git diff --check and progress-fragment validation pass; no changed/untracked
  file exceeds 20 MB. No files staged.

## CPU pre-draw optimization 4 (branch opt-cpu4, rejected, 2026-11-05)

- cpu_opt3 qualified at the inn on Ultra: capture total -0.24 ms/frame vs
  OFF spread 0.054 ms; default 1. CPU1/CPU2 remain 1.
- cpu_opt4 inn Ultra A/B: capture hook bodies +0.47 / +0.54 ms/frame vs OFF
  spread 0.104 ms. Rejected; evidence: captures/queue/logs/opt-cpu4-inn.*.log
  and its .cpu.md report. opt-final removes code, key, tests and opt_r5 job.
- Historical candidate used descriptor receipts, mapped-address/buffer-facts
  caches and persistent submit matrix slots; ABI and shader requests unchanged.
- Offline fixtures passed descriptor lifecycle and mapped/matrix sequences.
  Synthetic matrix lookup 0.1313 -> 0.1017 ms/frame did not predict game savings.
- Historical build/test passed 186/186 (183.44 s CTest); frozen lint receipts
  remain. The live rejection supersedes the former READY_FOR_LIVE status.

## CPU pre-draw optimization 3 (branch opt-cpu3, READY_FOR_LIVE, 2026-11-05)

- Qualified `cpu_opt2` now defaults to 1, retaining its A/B key. User-provided
  inn Ultra receipt: hook bodies -0.30 ms/frame, OFF spread 0.088 ms/frame;
  `captures/queue/logs/opt-cpu2-inn.1.052927.log.cpu.md`. `cpu_opt1` stays 1.
- Candidate `cpu_opt3=0` is layer only and NO_PIPELINE. ABI unchanged;
  shader request delta 0. No commit, game launch or queue staging.
- Detailed logging plus CPU timing adds pre-draw attribution for prepass
  observation, cascade membership, draw planning, alpha lookup, opaque/alpha material,
  BVH record publication, XFB commands, cascade draws and emissive draws.
  Nested scopes overlap; no new game timing result is claimed.
- Cache pipeline/table generation lookups, pipeline eligibility, model-position
  derivation from exact fresh bytes, complete VT alpha templates and BVH row
  address/stride preparation. Buffer lifecycle/binding/mapping epochs invalidate
  resource receipts, including recycled handles. Material bytes still read fresh;
  frame-local slots, triangle ranges, audits and lifetime uses still run each draw.
- Reduce hit-material mutex scope over mapped-input staging; shared publication
  and failure counters remain locked. Rejected mesh-arithmetic/finite-only caches
  avoided speculative overhead. No descriptor-result cache is used.
- Production-helper fixtures pass 240-frame row/model/material sequences with
  handle reuse, rebind, epochs, unmap/range failures and host writes; alpha checks
  preserve the original strict finite bounds. Mutex/staging fixtures pass 192 frames.
  Synthetic 5000-draw/1500-stream row preparation: 0.4202 -> 0.2408 ms/frame;
  registry reads 3,600,000 -> 1,060,860 over 120 frames. Not a game saving.
- One inn Ultra OFF/ON/OFF/ON session is prepared outside the queue at
  `tools/live_batches/opt_r4/cpu3/`: quiet readiness, 600 rendered frames/window,
  CPU1/CPU2 enabled. Every window requires complete CPU attribution receipts.
  Both ON windows must beat both OFF medians beyond their observed repeat spread
  for pre-draw and driver-excluded hook bodies. No fixed saving threshold.
- Frozen lint passes with three appended readiness queries and all old snapshot
  receipts preserved. Live CPU/GPU costs remain unmeasured.
- Final `scripts/build.ps1 -Test`: PASS 183/183, 184.67 s CTest, 3m59s total.
  Full build started after 10 minutes; checkout-local shader cache. Defaults and
  diff checks pass. All 31 changed/new Git candidates are below 20 MB.
- The pre-check merge-from-main attempt was blocked by sandbox permissions on
  shared Git metadata; HEAD and main both point to `2f209380`.

## CPU hook optimization 2 (branch opt-cpu2, READY_FOR_LIVE, 2026-11-05)

- Default-off `cpu_opt2`, layer-only, registered as NO_PIPELINE. ABI unchanged;
  shader request delta 0. No commit, game launch or queue staging.
- Requested 005111 Ultra static/pan: pre-draw 11.467/12.820, pass-after
  4.400/7.827, nested G-buffer material 2.777/3.838 ms/frame. Latest valid
  CPU1 OFF repeats: 8.696/8.624, 2.550/2.636, 2.064/2.032 respectively.
  Watch totals use rendered presents; nested timers must not be summed.
- Replace repeated alpha slot scans with capture-local indices; stage mesh
  eviction ranges and snapshot lifetime handles on the stack. Preserve first
  layout, descriptor order, extraction ranges and duplicate lifetime calls.
- Reuse G-buffer qualification within a call when runtime policy is unchanged;
  index material source records once per query and stage table additions without
  heap allocation. Reuse each TLAS slot's identical size receipt (one query/build).
- Detailed QPC scopes split pipeline, cascade, BVH classification, alpha records,
  material and driver-excluded dispatch tracking. Existing logs lacked these splits.
- Exact fixtures cover slots, records, full tables, toggles, layouts, extraction,
  qualification, rollback and lifetime order. Synthetic timings are not game savings.
- Prepared one inn Ultra OFF/ON/OFF/ON job, 600 frames/window, outside the queue.
  Ready phases require only a fresh quiet witness; every window requires CPU
  receipts. Frozen lint passes; 303 old readiness entries preserved, 3 appended.
- Synthetic alpha slots: 0.7530 -> 0.0196 ms/frame (5000 draws/1500 views);
  mesh ranges: 0.3860 -> 0.0508 (5000 draws/5 extracts); material records:
  1.6976 -> 0.0066 ms/query at 4096 rows, 0.0001 -> 0.0001 at 32 rows.
- Final `scripts/build.ps1 -Test`: PASS 180/180, 148.25 s CTest, 153 s total.
  Build began after the requested 15-minute wait; checkout-local shader cache.
  Defaults and diff checks pass. No changed/new Git candidate exceeds 20 MB.
- Full breakdown, measurement limits, cost gates and in-game check:
  `tools/live_batches/opt_r3/cpu2/README.md`. No live CPU/GPU saving is claimed.

## CPU scene scratch candidate (branch opt-cpu, READY_FOR_LIVE, 2026-11-05)

- Watch CPU submit is ms per reporting interval, not per frame: 58-185 / 120 =
  0.48-1.54 ms/frame, including driver submit/queue wait. Inn Ultra static/pan
  hook medians are 12.623/15.395 ms/frame, nested scene work included. See
  `tools/live_batches/opt_r3/cpu/README.md` for units, scoped costs and limitations.
- Added default-off `cpu_opt1`: retain four scene vectors and two membership
  tables; cache the per-draw pool-trim setting with its correct default. No GPU
  work, draw order, duplicate winner, lifetime contract, shader or ABI changes.
- Exact membership fixtures pass. Synthetic 5000-key lifecycle benchmark:
  0.3183 -> 0.0517 ms/build; no measured game saving claimed.
- One inn Ultra OFF/ON/OFF/ON job prepared outside the queue, quiet readiness
  only, 600 frames/window. Lint passes; READY_FOR_LIVE, readiness unproven.
- `scripts/build.ps1 -Test`: PASS 180/180, 273.19 s CTest (277 s total), using
  a writable checkout-local shader cache. Earlier shared-cache attempt discarded.
  Final analyzer fixtures, settings-default check and diff check also pass.
- No game, queue staging or commit. No changed/new Git candidate exceeds 20 MB.

## Readiness contracts and GI attribution (branch opt-attr, READY_FOR_LIVE, 2026-11-05)

- Fixed shared readiness selection: explicit CPU/push-value contracts generate no pipeline waits.
  Compact holds, scene-build policy and GI ray count no longer reconcile unrelated generations.
  Unknown keys require explicit evidence instead of an invented shader completion/quiet interval.
  Disabled box/exact-k passes have no completion requirement; rt_build=auto is emitted resolved.
- Regenerated opt_r1/scene_holds. Preserved its existing CPU proof and qualification status.
  Layer-only repeat toggles have no pipeline waits; real Ultra/High request changes retain theirs.
  Added 17 readiness queries to the frozen lint snapshot; all prior entries and other data preserved.
- Prepared tools/live_batches/opt_r2/gi_attr: one static Ultra inn launch, detailed_logging=1, dev=1.
  Six single-key OFF / ON / OFF brackets: hit direct, sun, emission, far bounce, second ray, material.
  Each pair keeps other Ultra dependencies on. OFF is pair-specific; an all-disabled common baseline
  would make sun/far/material attribution inert. Costs are conditional and must not be summed.
- 18 measured windows, 120 frames each, timer period 60. Only per-window GI trace cost gates.
  Signed attribution = ON minus the adjacent OFF mean; report both deltas and OFF drift.
  About 142.4 s after load, 300 s hard watchdog, no captures, estimated logs 5 MB.
  Reference Ultra inn trace is about 13.6 ms; no new measured cost or quality result is claimed.
- Offline: live_flow 72 tests and batch lint 12 tests pass; both generated batches lint cleanly.
  Shader request delta 0. No shader compile needed. No commit, queue staging or game launch.
  Pending: authorized live evidence only. This is a diagnostic job, not a rendering candidate.

## Round 1 optimization outcomes (branch opt-apply1, applied, 2026-11-05)

- Compact holds qualified: High inn saving 0.87 ms vs OFF spread 0.075 ms,
  float energy within OFF spread, identical BVH geometry fixtures. Default and
  every preset 1; release forces 1; developer A/B key retained, no player option.
- Roof reject removed: GI total OFF 21.75/21.58 vs ON 21.87/21.95 ms.
- Lazy origin normal removed: OFF 1.514/1.495 vs ON 1.634/1.520 ms.
  Both show no saving. IDs 1027/1028 reserved; tests/prewarm/jobs removed.
- GI attribution job removed: readiness cannot see cached pipelines.
- Historical scene analyzer flicker FAIL retained in TRACK; no detector pass claimed.
- Check High inn after layer restart; developer key permits same-run A/B.
- Shader request delta 0; ABI unchanged. `scripts/build.ps1 -Test` passes
  176/176 CTests in 150.24 s after schema regeneration and stale test cleanup.
- Release equivalence against main f3d3c41a passes: 203 identical, 11 equivalent,
  0 different; 1,041 assignments, 11 universal certificates; negative control PASS.
  Receipt: `.scratch/release-equivalence.json` (278.4 s).
- Defaults/schema and diff checks pass; no changed Git-visible file exceeds 20 MB.
- No commit, game launch or queue staging.

## Installer feedback and lifecycle speed (branch setup-speed, qualification blocked, 2026-11-04)

- Setup now paints a clear status before extraction, Install, Inspect, Recover and Commit;
  uninstall paints its backup/removal status once its progress form exists.
  Inno 6.7.3 already pumps messages inside synchronous Exec; no custom polling or reentrancy.
  Reference: [Inno wait loop](https://github.com/jrsoftware/issrc/blob/is-6_7_3/Projects/Src/Setup.InstFunc.pas#L592).
- Replaced repeated PowerShell provider ancestor walks with a small in-process C# filesystem
  walk and streamed .NET SHA256. No path approval cache. Every ancestor/game-path check,
  both source/stage hash passes, staging, snapshots, journal writes and rollback remain.
- Real-payload timing is BLOCKED by the explicit 20 MB file limit: dxcompiler.dll,
  nvngx_dlss.dll and nvngx_dlssd.dll exceed it. Initial temporary copies of these were removed.
  No exception was assumed. The user's reported real-payload baseline is about 75 s wall / 47 s CPU.
- Measured a bounded fixture with the real 219 paths and 216 unchanged files (35.09 MB),
  replacing only those three DLLs with tiny placeholders and regenerating fixture hashes.
  Child process discovery was mocked as in the existing fixtures because the real game/launcher
  are running. Only disposable TEMP destinations and isolated LOCALAPPDATA were used.
  These figures do not qualify real-payload performance or the well-under-15 s target.

| Phase | Before (s) | After (s) |
|---|---:|---:|
| Preflight, including source verification | 19.710 | 3.473 |
| Staging copy | 17.976 | 4.955 |
| Staged verification | 5.801 | 1.690 |
| Backup snapshot (fresh install) | 2.390 | 0.592 |
| Journal and destination apply | 16.915 | 3.520 |
| Install -DeferCommit, including process startup | 63.103 | 14.508 |
| Separate Commit, including process startup | 17.470 | 3.287 |

- SHA256 time inside the above phases: 1.085 -> 0.628 s total. Path/provider overhead dominates.
- Final checks: release_lifecycle_test.py 16/16 PASS (41.59 s), release_setup_test.py
  4/4 PASS (2.35 s). New fixture creates a junction after preflight and confirms rechecking
  rejects it before journalling or touching its target. git diff --check PASS.
- Full verify_rc.ps1 was invoked as requested; exit 1 at its running-game/launcher build guard.
  No process was stopped. Its generated failure report is retained in .scratch/verify-rc-blocked.md;
  the tracked RC_CHECK.md was restored to its prior bytes. Separate tiny compile-only fixture
  also failed before compilation: sandbox denies reading the pinned ISCC.exe. No setup ran.
- Remaining: authorize oversized disposable artifacts/payload copies, obtain compiler access,
  then profile the real payload and rerun full qualification when the game/launcher are closed.
  Existing install and user settings untouched; no game launched; no commit.

## Release logging (branch quiet-log, offline verification, 2026-11-04)

- Periodic `log_stats` covers emissive/census, OMM, alpha/hair, cascade, geometry/BVH,
  bridge motion, local lights, water/sun and GPU/pacing/watch reports (77 report call sites).
  Statistics windows still reset while quiet, including side-effecting counters.
- Startup, configuration, state changes, warnings/errors and device-fault reports remain visible.
  Routine VRAM usage is limited to 1 line/minute; pressure/refusal reports remain visible.
- Original user baseline: about 1.7 MB / 7 minutes = 0.243 MB/minute. Exact reduction is
  unavailable: installed log contents were inaccessible. Quiet-state cadence is an offline
  prediction (about 1 VRAM summary/minute plus events), not a measured game result.
- Previous verification: `scripts/build.ps1 -Test` 172/172 pass (186 s tests, 8 s build);
  `-ReleaseLayer` PASS, diagnostics OFF (23 s). Shader cache needed a local snapshot.

## Detailed logging switch (branch quiet-log, offline verification, 2026-11-04)

- Added player key `detailed_logging`, label **Detailed logging**, exact bug-report help text,
  Off in every preset. Both launcher UIs show it beside Report a problem in Help & support;
  it needs a game restart and does not mark the rendering preset Custom.
- The setting is latched at startup. It joins `dev`, `rt_stats`, `ls_stats` and positive
  `rt_stats_period_frames` in the statistics gate; SDK info also accepts Detailed logging.
- Log-only per-frame Vulkan GPU timestamp recording and readback now use the same gate
  across NGX/RR, RR post, FG input copy, GI/RT/local shadows, SHaRC, water, hair and capture.
  No feature depends on those timestamps; functional transform-feedback queries remain.
  Per-allocation fault-range lines are gated; ranges remain in memory for fault reports.
- Startup records `[cfg] detailed logging: 0/1`. Both report dialogs inspect that last-session
  marker and suggest enabling logging, restarting and reproducing when it was Off.
- Updated USER_SETTINGS, packaged README and Nexus reporting steps. Defaults/schema generated.
  No ABI or shader changes; `shaders/variants.txt` request delta 0. No live GPU cost claimed.
- All 19 active/final live jobs explicitly request `detailed_logging=1` at startup, with generator
  coverage and original cadence/gates retained. Frozen lint: active 17/17, final 2/2 pass;
  historical evidence limitations remain. Batch lint: 10 tests pass.
  Readiness matching ignores only the logging key because it cannot build pipelines.
- Follow-up `scripts/build.ps1 -Test`: 173/173 PASS (184.22 s tests, 0 s incremental build;
  initial recompilation 40 s). Settings/schema and launcher UI pass; local
  `BG3RTX_SHADER_CACHE` snapshot used as in the previous verification.
- Check when separately authorized: Help & support > Detailed logging On, save, restart,
  reproduce, then Report a problem. Off keeps quiet play; On restores diagnostic cost lines.
  No game launched and no commit made.

## Runtime classification tables included (branch pkg-runtime, kept offline, 2026-11-04)

- Fixed release packaging: include `shaders/rt/alpha_fs.txt` and `camera_fade_fs.txt`.
  Missing alpha classifications made foliage opaque in ray tracing in the release.
- Audited layer/core install-relative reads. Asset DB, settings, shader requests/includes,
  core, DXC and production Streamline inputs are covered. `vt_alpha` is optional:
  no indices disables offline masks; `hair_grooms` absence retains the card path.
  Captures, caches, developer paths, optional Aftermath and SHaRC headers stay excluded.
- Added one registered runtime package guard deriving 154 dependencies from source,
  checked against the actual dry-run plan. Removing either classification table fails.
  `verify_rc.ps1` runs it before passing the Manifest row.
- Payload expectations: 219 -> 221 files. Requested dry run: 221 files, zero manifest
  differences, 13 missing build inputs in this worktree; this is not full RC qualification.
- Validation: runtime guard 1/1, licences 8/8, install 10/10, setup 4/4 passed.
  Shader request delta: 0; runtime/GPU cost unchanged.
- In-game follow-up after an authorized release rebuild: check foliage cutout shadows
  and camera-fade classifications. No game launched or installed copy modified here.
  No commit created.

## Final release qualification (branch final-qual, READY_FOR_LIVE, 2026-11-04)

- Prepared from main ab66c855. No commit, game launch, real queue staging or authorization receipt.
- One batch, two launches: final-s8-ultra-high (rel1 Ultra + rel2 High at Water Queens House),
  final-stock-hair-dialogue1 (rel3 Ultra stock cards). Actual exported release profiles, dev=0.
- Expected 3.72 / 3.32 minutes including 120 s load, hard timeout 300 s each; about 200 MB total
  (three energy traces at 60 MB allowance plus two 10 MB logs). No raw floats/bursts/shader dumps.
- Preserved release smoke gates, patterns/expect/expect_every, five S8 camera windows per preset,
  master toggle, stock hair/scalp records and zero bounds/capacity/scene-wait errors. No device loss
  or crash. High OMM same-ray compared > 0, unavailable 0, violations 0 in a separate diagnostic.
- All measured windows reset stats at motion-ready and acknowledge one period. visible_unfocused_fg;
  no diagnostics or captures in cost windows. Existing tools generate/reset/wait and restore settings.
- predicted_final.json contains reference per-pass costs from rel1 20:58:53 and rel3 21:03:04,
  High rel2 12:13:05, all gates and shader-change predictions. Shader request delta 0.
- Costs expected within observed scene ranges; report signed changes and explain any exception.
  Observed sparse/moving ranges are not statistical noise thresholds. No build/compile overlap.
- Post analysis automatically reads each energy trace and writes <log>.final.json: GPU pass deltas,
  float pre-post C energy (rrpost slot 2), signed historical energy deltas where available.
- Dialogue baseline luminance 0.3743881342. Rel1/rel2 have no float capture: historical S8 energy
  comparison is UNAVAILABLE, not PASS. No baseline noise repeats exist. Retain new float summaries.
- Expected energy within noise globally; corrected cutout visibility has scene-dependent sign,
  invalid history uses fresh/native (can brighten former black gaps); half-float clamps cap tails.
  Any material signed change needs physical attribution/PT; do not accept unexplained darkening.
- Live-flow preflight: all 5 source/final jobs PASS against 79 VALID logs. Frozen lint: all PASS;
  only 6 required readiness queries and missing regexes appended. Tests: release_qual 4/4,
  batch_jobs_lint 8/8, live_flow 69/69, test_registration 1/1. Frozen CLI avoids raw-log caches. No live quality pass is claimed.
- Before queueing: coordinator commits predictions, merges/builds test-main, obtains explicit user
  steering and freezes the normal job/tree/build authorization. From main, exact queue command:
  `Copy-Item -LiteralPath @('tools/live_batches/release_qual/final/jobs/final-s8-ultra-high.json', 'tools/live_batches/release_qual/final/jobs/final-stock-hair-dialogue1.json') -Destination captures/queue/pending/`
- Queue only those two final jobs into the existing serial runner. Do not queue the source receipts
  or start another runner. Cold/missed readiness fails fast within the five-minute launch cap.
- Inspect High cutout edges/holes, Ultra water/edges, stock hair cards and face/neck shadows in the
  static captures. Occluded presents/frame time cannot qualify GPU cost or visual output.
