# BG3RTX progress - 2026-11-03 evening

## User questions and decisions

- Open: decisions in GUI_AUDIT_20261102; project licence; NVIDIA notification for the beta.
- Open: SHaRC distribution form, Larian data terms, and obtaining missing DXC/Vulkan-Headers notices.
- Open look: soften the S8 water sun-shadow edge or keep native glint suppression in shade?
- Open, not blocking: rt_hit_emission 1 or 2? Ultra keeps 1; 2 remains developer only.
- Lives are paused by the user until testing is fixed. A fresh batch instruction is required to resume.

## Report 2026-11-03

- Bundles 6, 7 and 8 merged through merge-green; rendering main is eca2023b, ABI 26.
- Testing repairs are on main; their first live use still needs validation. No live is authorized here.
- Dialogue shadow fix and Ultra OMM/box qualification stand; the latest combined smoke was INVALID.
- Strand live enable still risks device loss. Keep it off until the pending hair-reload-fix lands.
- User reports much steadier DoF with rr_post_temporal; promotion waits for valid Dialogue3 evidence.
- S8 temporal cost is about 0.12 ms; sparse water-hit reflection cost fell about 2.1 -> 0.53 ms.
- Medium rt_trace_scale=0.5 saved about 0.18 ms RT; these exploratory costs are not quality passes.
- Pak discovery is fixed; camp participation needs live evidence. Water Fresnel is READY_FOR_LIVE.
- Hair batch converted 519/1,217 styles; 698 remain under study. SHaRC is inconclusive and stays off.
- Resume with crash fix, hv5, smoke, Fresnel, pak join, Dialogue3 and hit-cost quality, in that order.

## Cached pipeline readiness (branch readiness-cached, kept offline, 2026-11-04)
- Acknowledge reported cfg values; resident/no-op keys need no build announcement. Reconcile outstanding affected families and require exact completion for every new build.
- Five failed warm phases replay PASS: hv6 25 ms, hv5 79 ms, wsf1 66 ms, cp4 86 ms, pt-dialogue3 57 ms after write; stale startup alternatives explained the old pending records.
- Bound proven waits by same-save/key median plus margin from the preflight cache; retain finite bounds without samples. Timeout names missing cfg/pipeline/producer patterns.
- Offline checks only; no game launch, shader/ABI changes or git metadata writes. Rendering qualification remains pending.

## Automatic pipeline readiness (branch queue-refusal-record, kept offline, 2026-11-04)

- Shared readiness generation: 25 active jobs pass lint/preflight; 84 historical jobs inactive. Combined S8/camp split; maximum scheduled + VALID-median wait 876.510 s. Per-job before/after: tools/live_batches/readiness_report.json; hv1-hv5 used (no local hv6).
- Nine mappings explicitly use conservative generic shader completion; timing/quality remain unproven. Six extraction dryruns and 147/147 CTests pass (144.19 s); stale settings executable rebuilt and old timeout-cap assertion corrected. No game, queue submission or commit.

## Trace window sizing (branch pt-pairs-traces, READY_FOR_LIVE, 2026-11-04)
- 004032 missed on-02 dispatch: 2.1 s window lasted 3.803 s; five written latencies were 0.081-0.097 s. VALID p95 0.317 s / 439 requests; written precedes dump completion.
- Budget 4 s dispatch plus serialized export/write across phases; energy 2.1 -> 6.1 s. Keep all gates/traces; 10 balanced pairs >= derived minimum 8, total 864.9 s including readiness/loading (12 would be 927.7 s).
- Offline preflight, 45 live-flow and 9 paired tests pass. No game, queue, commit or git metadata writes; live quality remains unproven.

## Temporal post pairs (branch pt-pairs-result, rejected for promotion, 2026-11-04)
- VALID 032335, 10 pairs: all 12 primary tests FAIL (p 0.171875?0.828125 vs 0.00416667); presented registration fails 9/20, face support 0.
- Pre-post float energy FAIL (+0.1492% paired mean); background join FAIL. Temporal cost 0.152?0.153 ms. Keep rr_post_temporal off/Advanced; S8 quality missing.
- Result: tools/live_batches/post_temporal/RESULT_pairs_032335.md + JSON; 940 analyzed files deleted (4,560,799,306 bytes), exact manifest retained.
- Numeric presented sorting fixed; paired tests 9/9. Offline only; no preset change, game launch, commit or git metadata write.

## Camp recipe gate (pak-recipe-cull, offline, 2026-11-04)
- VALID cp4: 22,686/22,686 unique observations culled; 5.655% of attempts, 3.762% of draws; 0 additional pak publication share proved.
- Count 39,408 is HLOD_VT flags 0, not audited HLOD_GM mask 4; 24/39 unique completions have no eligible template in their count bucket. Exact pair/template/cull split absent from old log; no shading defect proved.
- Preserve physical gates/native precedence; stop raw witness scheduling for recipe-rejected candidates and expose first-rejection reasons. Regression covers culled recipe and compatible publication.
- Same-cohort ON prediction: witness_ok/published_records/pak basis ready 0/0/0; 27,172 deferrals are cumulative, reduction unknown. Audit: tools/live_batches/cov_paks_compact/recipe-cull-audit.json.
- Host build, 6 focused CTests and real 40,983-row parser pass; ABI/shader request delta 0, key stays off. No game, commit or GPU cost measurement.

## Frozen lint corpus (branch lint-corpus-freeze, offline, 2026-11-04)
- CTest uses 223 KB of derived statistics from 90 VALID runs; explicit refresh: python -m tools.live_batches.lint_snapshot. Preflight retains live corpus.
- Shared generators refreshed active jobs; combine preserves original source slices; no game launched, no git metadata writes. Shader request delta 0.
- Fresnel abort bound: 151 / (60 x 270255.5) = 9.31218e-6 from 2,529 VALID intervals; invalid remains 0.
- wsf1 partial ON ownership 6,914,793; static compose 0.433 -> 0.458 ms (+0.025 ms); whole-water cost and qualification unavailable.
- Validation: 147/147 CTests (92.12 s, local shader cache), 26/26 active lint, 6/6 combined dryruns; details: tools/live_batches/water_surface_fresnel/RESULT_wsf1_031444.md.

## Dithered strand tips (branch claude/hair-tips, offline, 2026-11-04)
- Added rt_hair_groom_tips, live Developer switch, off in every preset; header word 15 bit 0.
- G-buffer and velocity evaluate the native mask alpha at the cached strand UV and share the
  native temporal dither. No card grazing factor; the pixel record remains unchanged.
- Dither twin: 1600 samples per alpha, shares 0/0.25/0.5/0.75/1, all exact (tolerance 0.05).
- Required build.ps1 -Test passed: 147/147 CTests (381.45 s), with worktree-local shader cache.
- Both requested shaders compile and pass spirv-val. Variant request delta: 0.
- Identity: 256/262 closures unchanged; explicit six-request HEAD comparison 3/6 identical.
  Combined: 259/262 requests byte-identical; only rt/hair_primary_* differ (including count).
- Initial CTest found stale settings schema (regenerated) and a read-only shared shader cache;
  rerun uses BG3RTX_SHADER_CACHE=build/shader_cache within this worktree.
- No full identity run, commit or game launch. Live quality and GPU ms remain unmeasured.
- Check Test Dialogue1 strand tips/temporal edges after hair reload safety is qualified.

## Strand hair fail-safe for the first level load (branch claude/hair-startup-load, READY_FOR_LIVE, 2026-11-04)

- Strand hair fail-safe for the first level load (hair_crowd hc1 2026-11-04: device lost 10 frames after the first
  strand publish, rt_hair_groom_primary latched at startup). `hair_primary::LoadGuard` now opens its 300-frame quiet
  window on the first G-buffer image too (before: only on a change, so the first load had none). bvhsplit publishes
  a groom draw key only from the second update of its points range. Layer change (game restart). Cause of the fault
  not confirmed: the live check is hc1 again (no device lost; "level load (first or new G-buffer images)" logged once
  at the first publish; strand draws only after the window).

## Strand shadow transmittance, C2 (branch claude/hair-shadow-transmittance, offline verified, live unqualified, 2026-11-04)
- Key rt_hair_groom_shadow_t stays off. Specialization 1026, kSpecIdHairGroomShadowT, replaces the dormant
  RT_GROOM_SHADOW_T_REFERENCE copy. At 0 the original rt_groom_trace first-hit expression runs; at 1 the
  stochastic d_f * prod(a_f) visibility runs. GI and native water carry the value in their pipeline cache key.
- Production and tools/hair_shadow_gpu_check.hlsl now use the same hp_crossing helper (8 recent candidates),
  replacing C2's separate 4-crossing implementation. The primary colour samples and measured a_f publication remain.
- No shader requests added (delta 0), no ABI change. Experimental prewarming includes the new constant.
- tools/hair_shadow_check.py checks ordinary requests against main without overlays or reference defines.
  Only raw DIFFERENT requests can use strict retirement/default equivalence: baseline IDs 1017-1019 = 0,
  ID 1026 = 0 on both sides. All other surviving specializations remain variable. Unit checks cover nonzero
  C2 defaults, changed arithmetic, invalid modules, entry mismatch and preservation of other specializations.
- Full build passed: 147/147 CTests. Unchanged water_surface_shader_test: 16/16 OFF accepted (1 raw,
  15 strict equivalent), 2/2 raw ordinary accepted (1 raw, 1 equivalent), 16/16 production modules validated.
- Ordinary proof vs main 9cb1e3a: 113/255 requests byte-identical, 142 equivalent at default; 0 failed.
- Clean 009f60f water rerun passed with the same counts and zero DXC compiles.
  The first 009f60f run was INVALID at the source-freeze check after a concurrent prewarm-header edit.
- Hair L2 keeps every participation, energy, producer and cost gate, hold 90 and 300-frame windows. Two PNG
  pairs per save (base/shadow, base/tips), four cost windows and one combined candidate warmup.
- Dialogue1: 336.30 s plan + 216.86 s readiness + 120 s load = 673.16 s. Campsite: 284.60 + 210.20 + 120 =
  614.80 s. Both under 780 s including allowance; two-launch total 21.5 min, 1,780 MB estimated.
- Dryrun passed all checks with MaxMinutes 26 (aggregate). Jobs remain outside watched queue inboxes.
- Current GPU bench passed: 100% of 3000 ray crossing counts match CPU; transmitted trials 174623 vs 175064.4
  expected, sigma 353.2 (1.25 sigma error). No live run or measured GPU cost; not a quality pass.
- User check after authorization and hair safety prerequisites: Test Dialogue1 then Campsite, skin/neck under
  hair in shadow pair, tip fade in tips pair, float pre-post energy and GPU pass ms in the retained gates.
- No commit, game launch or queue submission performed.

## Strand probe and G-buffer trace only this frame's groom TLAS (branch claude/hair-probe-fix, READY_FOR_LIVE, 2026-11-04)

- hv5 lost the device in the load with no strand swap (the probe was the only groom trace in the game's command
  buffers); hv6 probes counted 0 strand samples next to 95670 card samples. Both traced a staged publish up to 2 frames
  old: the groom TLAS is camera-relative (an older one misses the strands) and can reference BLAS ranges freed by a
  LOD change or compaction after the scene slots retired.
- Probe and order-mode G-buffer header now take only the publish of this frame (else no probe / cards); no probe during
  the 300-frame load window; groom BLAS, points and LOD frees wait 3 more frames (bvhsplit kGroomFreeLag).
- The coverage line ends with "probe frames refused: no publish of the frame N, older publish N, load quiet N".
- Live check: hv6 (no device lost in the load, strand samples > 0 for drawn hair, refusal counts).

## Strand indirect volume scattering (branch claude/hair-fibre-gi, offline candidate, 2026-11-04)

- Added live Developer key rt_hair_fibre_gi, off in every preset. No ABI change. No game or commits.
- GI composite replaces its Lambert response only on current sealed class 5 strand pixels at matching depth.
  It already has the gathered irradiance; changing the earlier D-LS fibre pass would need another GI binding.
- A = S + k_volume M: integrate dual forward/backward scattering with a_f, a_b, d_f=d_b=0.7 and existing k,
  then calibrate the multiple term against diffuse-incidence BSDF slabs. The directional k alone is insufficient.
  White endpoint is 1 by conservation. Colour below 0.02 and beta outside 0.35-0.7 remain clamped.
- Independent coloured slab max channel errors: CHAR_Hair 1.3882%; mid colour 0.7563%; light colour 0.8466%.
  White furnace 1.0; independent finite 20,000-bounce white walk 0.9842. GPU/CPU max relative error 1.46e-7.
- Key-off identity: 7/7 affected ordinary requests byte-identical to 97e2487b4a26. Explicit compile/spirv-val 9/9.
  Two extra composite requests only, RT_HAIR_FIBRE_GI_REFERENCE=1 (extra buffer-address capabilities).
- Build succeeds; final full CTest: 145/146, including both hair CPU and GPU tests. Defaults/schema regenerated.
  The inherited water_surface_shader_test fails 13/16 OFF comparisons against merge-base 009f60f; its inputs
  are unchanged from starting HEAD 97e2487. The same unmodified gate with --baseline HEAD passes 16/16 OFF,
  2/2 raw ordinary and 16/16 production validation. Full-suite acceptance is not claimed; water is untouched.
- One earlier suite was invalidated by the final current-frame header check changing sources during a frozen
  water gate; the final run used frozen sources. Shader cache is local to this worktree; shared artifacts read only.
- Live and cost unmeasured, per task's no-game instruction. Later check: Test Dialogue1, indirect-lit strands,
  same-run rt_hair_fibre_gi A/B with primary strands enabled; inspect hue/energy against the slab expectation.
- Full derivation, paper source and calibrated domain: RTX_HAIR_PRIMARY.md section 3, M4.1.

## Strand diagnosis preparation (branch claude/hair-diag-live, READY_FOR_LIVE, 2026-11-04)

- Split hv5 reload stages from hv6 first-load/order 0/1/0/1/tips/coverage diagnosis.
- Per-launch plan + readiness: hv5 668.3 s, hv6 613.3 s; both below 780 s / runner 900 s cap.
- Every swapped draw must meet the logged coverage bound. Low coverage retaining cards is safe.
- Conditional participation/cost uses complete coverage periods; no-swap does not require GPU cost.
- Eight consecutive-frame bursts and three PNG traces compare the same player-head close-up;
  three cards repeats derive the edge-flicker envelope. Full energy/MV/PT quality remains unqualified.
- hc2 had 300 G-buffer draws/period and no GPU cost: publish-owned timestamp reset can miss
  G-buffer CPU recording; logs do not identify query availability. Layer fix remains separate.
- Reached hc2 scene-wait windows pass; missing later windows followed fail-fast aborts.
- No game, live queue submission, commit or Git metadata modification. Predicted.json still needs
  committing before a future authorized live. Offline verification receipts are scratch-only.
- Final offline checks PASS: queue_gates, hair_diagnostic (4), hair_validation (17), live_flow (43),
  batch_jobs_lint (2), both fake-queue branches and shared lint. Dry-run estimate 25.4 min / 1,430 MB.
- Scratch receipt: %TEMP%/bg3rtx-hair-diag-20261104-final/live-flow.json; fake receipts grant no live authority.

## Per-draw strand coverage (branch claude/hair-coverage-gate, READY_FOR_LIVE, 2026-11-04)

- G-buffer/velocity and the extra prepass rule now require fresh per-key precise occlusion samples. Cards remain
  until proven; failed ratios recover without strikes or retry waits. Pixel-record feedback is diagnostic.
- Native card query plus a store-free groom trace probe in the camera prepass. Query reset and probe header update
  run in that game's command buffer, outside the pass. Actual successful submission, recording generation,
  completion and both availabilities gate readback; partial keys, old scenes and active game queries fail closed.
- `rt_hair_groom_primary` only; no new key/ABI. Off at device startup creates no primary resources or variants and changes no draws.
  `occlusionQueryPrecise` is requested at device creation when supported (rtscene); if absent: UNAVAILABLE once, cards.
- Strands on screen only in header order G-buffer (`rt_hair_groom_primary_order=1`): in prepass order the G-buffer can
  read another frame's header than the probe proved (hc2: 296/300 adjacent tags, player half bald), so cards stay.
- Strand G-buffer timestamps reset in the G-buffer pass's own command buffer (hc2 had no GPU cost line).
- Bound 0.8913102349775665 = minimum offline ratio 0.9105471772550376 - modeled count noise 0.01923694227747121.
  Regenerated 877 passing styles / 2,631 views, zero failures; Minthara LOD1 side is limiting. See section 5b and
  claude-hair-coverage-bound.json. Noise assumes independent spatial samples; temporal noise is unmeasured.
- New shader request +1; variants.txt delta 0. Existing primary G-buffer/velocity/count: 3/3 byte-identical to
  HEAD 3b69d480da6d by identical inputs. Probe: 1/1 compiled and SPIR-V valid through shader_identity.py.
- Probe estimate: 0.025 ms per 100k card fragments / 0.4 ms per 1.6M, plus duplicate vertex/raster/query work.
  No numeric strand G-buffer GPU lines exist in supplied hc1/hc2 logs, so a measured comparison is unavailable.
- Offline validation: build.ps1 -Test passed 147/147 CTests (113.23 s); 25 new gate/bound assertions.
  Build 20 s, BelowNormal, SDKs from main; shader cache local to this worktree. No commits or game launches.
- In-game confirmation remains unrun: camp player/Minthara hair should retain cards below the bound; the exact
  300-frame coverage line must show fresh ratios and swapped-frame counts. Strict native depth tests may keep
  cards permanently; native comparison is preserved. One reset-owning prepass recording per frame, up to 128
  draw pairs; additional recordings/overflow keep cards. Empirical cost and temporal/dither noise remain unmeasured.

## Surface rejection diagnostics (branch water-fresnel-zero2, READY_FOR_LIVE, 2026-11-03)
- Second live: clean 0 despite 120/120 patched draws and 99.53% owned. No offline proof of a record/spec mismatch; no speculative shading fix.
- w7 reason bits report independent foam colour/F0/roughness vetoes, foam-texture and edge/flow-mask sources, native/missing marker, layout and FS/compose specialization; no stride, ABI or request delta.
- Counters are device-local/group-reduced; push flag independent of SpecId 1025 exposes an unset compose specialization. Build and 6/6 targeted CTests pass; CPU 1,237,748 checks and all patched FS audits/spirv-val pass. Compose: 16/16 arithmetic-OFF and 2/2 raw ordinary identical to HEAD; 16/16 production validated, all counter contracts pass.
- Predict ON: clean owned may remain 0, marked owned > 0, at least one foam veto > 0; correctly wired layout/spec-off 0. Native marker alone cannot identify its family.
- No game launched or git metadata changed; key remains off. S8 pool rim A/B and measured compose GPU cost remain pending.
## Coverage-layer replacement (water-fresnel-zero2, READY_FOR_LIVE, 2026-11-03)
- Supersedes clean-only design: authored BridgePools/Fountain c floors 7.96214e-8/1.19432e-7; exactly-zero share 0%, all 27 foam/flow mips decoded (docs/evidence/water-foam-coverage.md).
- Native foam c plus independent clean BRDF (1-c); coefficient sum 1 for every coverage, exact endpoints, full-float shading; existing counters now coverage bins and weighted ownership, runtime finite/domain/8-epsilon partition check.
- Core/layer build and 2/2 final water CTests pass, 1,256,907 CPU checks/all seven FS forms validated; 148/148 CTests covered (shader snapshot invalidated by final FS edit, cached rerun passes); OFF 16/16, raw 2/2 identical, production 16/16 valid, counter contracts pass. Key off; ABI/stride/variant request delta 0.
- Prepared single unqueued wsf1: 127.4 s plus load, real Ultra static 0/1/0/1 float shots and held E/Q; per-ON weighted ownership > 0/invalid 0/partition, all eight post-water float shots required; lint/fake dryrun pass, about 490 MB.
- No game or git metadata writes. Inspect S8 shallow rim/foam; model mean may change. Dual FS forward cost unmeasured and excluded from existing compose timer; report that limit and compose GPU ms. Lives remain paused.

## Foam-floor predicate (branch water-fresnel-zero, READY_FOR_LIVE, 2026-11-03)
- wsf1 pool c4e92ccd provisioned; ON selects 120 draws/window, missing 0; clean owned 0, invalid 0. Sea 7b44a567 stays native; 99c1fcef occurs before save load.
- Final foam SSA 751 floors abs(signal) to 1e-4 before pow (~3.19e-8 at template values): exact final zero excludes absent foam. Test exact pre-floor zero independently for colour/F0/roughness; retain final-zero fallback and native inputs, no epsilon.
- Layer/test build and 4/4 water CTests pass, including seven FS forms validated and emitted-predicate cases for zero, tiny real foam, multiplier zero and key OFF. ABI/stride/request delta 0; no compose shader edit.
- Predict both ON windows clean owned > 0, invalid 0; fully clean pool ~20% of water records (~56k/frame, old 2.05% view / live ~10.1% records). Actual runtime masks/share are unmeasured; nonzero flow foam stays native.
- Off by default; restart layer, inspect S8 pool rim using same-run wsf1 A/B. No game launched, queue submitted or commit made; GPU cost and live participation remain open.

## FPS windows (branch tools-fps-windows, kept offline, 2026-11-03)
- Per-save diagnostic-state FPS uses 102 readable VALID logs, conservative p10 and a documented cost fallback; lint and fail-fast share the model.
- su1 diag-omm: 9.1 -> 16.4 s. cp4 ON observations: 19.1 -> 32.8 s each. Gates unchanged.
- Regenerated 15 active jobs plus combined manifests; separate crowd plans prevent save-dependent overwrites. Full window table: tools/live_batches/fps_windows_report.md.
- Replay checked 108 jobs; 106 lack some historical gate evidence. Exact failures: captures/offline/fps_windows_replay.json. No rendering pass claimed.
- lean_dryrun delegates duration rules; 5 hair jobs and both 5-job fake-queue paths pass. Aggregate estimate 71.1 min exceeds the retained 65 min limit.
- cp4 and combined camp/S8 remain blocked by readiness/launch budgets; combined receipts now retain these failures. No game, queue submission or Git metadata writes.
- Validation: 147/147 CTests pass; 58 focused tooling tests pass. Shader request delta 0.
- Bundle 12: reconciled tools-fps-windows with water-fresnel-zero2; wsf1 generator derives 11.9 s static windows and 1.1 s restore, preserving gates and predicted.json.
- Repaired gpu_opt1 generator indentation and split save-dependent plans; regenerated 8 failing jobs to pass all 22 active-job lints. Linked SHA-256 into pak_uv_join_test for pakjoin-publish.
- Bundle 12 validation: full build and 147/147 CTests pass (verified process exit 0); water shader identity passes, shader request delta 0. No game, queue submission or Git metadata writes.

## Long-session and device-loss audit (branch stability-audit, offline verified, 2026-11-03)

- Audited 87 queue logs plus the user log; no provable VRAM leak. Water BLAS peak 2211 -> 437 MB; mesh remains capped at 2047 MB.
- Fixed 14 silent device-loss results in 6 layer files; all 19 submit/wait sites audited. No ABI/settings/shader changes; variants delta 0.
- Evidence: docs/evidence/long-session-audit-20261103.md and CSV; missing live counts/CPU timing and shader-edit CPU retention have exact deciding observables.
- Validation: full build passed (55 s); 147/147 CTests passed (198.64 s). No game launch, live qualification or Git metadata writes.

## Release licence audit (branch release-licences, blocked, 2026-11-03)
- Preserved 11 local notice texts verbatim; recorded sources/hashes and protected bytes from Git normalization.
- Added package manifest coverage for 10 third-party DLLs and 6 compiled SDK components; uncovered notices fail before writes.
- Selected DXC LICENSE.TXT and Vulkan-Headers LICENSE.md / LICENSES/{Apache-2.0,MIT}.txt remain missing; old sample NCSA text is reference only.
- Packaging CTests: 3/3 passed (40.13 s); offline beta blocker status in docs/release/RELEASE_OFFLINE_STATUS.md.
- No game, download, player archive, commit or Git metadata mutation; release remains paused.
## Version-matched notices integrated (branch release-licences, kept, 2026-11-03)
- Resolved the missing-text blockers above: five supplied DXC/Vulkan texts retained verbatim, with URLs, revisions and SHA-256; DXC identifies University of Illinois/NCSA plus included third-party terms.
- Real-text enforcement and packaging CTests passed 3/3 (38.98 s); ListOnly validated 17 entries. No downloads, game, player archive or Git metadata writes; broader release decisions remain open.

## Dialogue3 post temporal analysis (branch pt-d3-analysis, rejected promotion, 2026-11-03)
- VALID pt-dialogue3.1.210703: rendered bright-contour residual 6.2886/3.5164 OFF -> 0.9347/1.3772 ON, but OFF-noise gate fails.
- Hair OFF 7.5164/33.2349 -> ON 9.6760/1.5008; framing/animation confounding, presented registration rejected, face support empty.
- Float scene means pass OFF spread 0.002971; fixed-background energy cannot qualify without trace registration. Temporal medians 0.1685/0.1725 ms.
- Keep Ultra off; S8 cost 0.12 ms is not the required quality evidence. Check Test Dialogue3 hair over DoF/bright contours; see RESULT_20261103_D3_valid.md/JSON.
- Offline analysis only; no game, git metadata, preset, ABI or shader request changes. Raw deletions recorded in result JSON after reader checks.
- Drift test repaired: 12 adjacent counterbalanced pairs, all-OFF frozen masks; exact sign gate at 0.05 / 12, minimum 8 pairs.
- Derived plan 338.9 s; loading/readiness worst case 818.9 s < 900 s. Dialogue3 live-flow lint passes against 22 valid logs.
- Retained run: INSUFFICIENT (2 pairs); synthetic effect + drift passes, no effect + drift fails. Nine CPU fixtures pass; no promotion.
- Offline only; no game, queue, git metadata or capture deletion. READY_FOR_LIVE test design; evidence still pending.

## Pak UV pending fix (branch pakjoin-pending, READY_FOR_LIVE, 2026-11-03)
- cp4 raw UV format 83 is half2: raw float2 binding stayed 0xff, blocking XFB enqueue and retrieval (0 attempts, 422,707 pending/draws).
- Keep raw CPU witness gates; decoded XFB keys and invalidation cover all per-vertex bindings. Publication gates and default-off key stay intact; ABI/shader request delta 0.
- Production readback fixture replays discovery -> enqueue -> copy -> submit retirement -> later-recording attempt, including packed UV, changed binding and unique/missing/ambiguous outcomes.
- cp4 ON 9.1 -> 19.1 s: rounded observed 9 s line bound plus settings/straddle; require attempts > 0. Predict unique minority, missing majority, possible ambiguity; full witnesses may stay 0.
- Host build, 7 focused CTests and cp4 offline preflight pass; no game launch, commit or GPU cost measurement. Earlier fragment is folded in docs/archive/PROGRESS_20261103_evening.md.

## Native hair card cleanup (branch hair-cards-retire, VERIFIED, 2026-11-03)
- Retired visibility (null), cheap and GI cull (energy failures), quarantined mean; retained settings/presets match main exactly.
- Main 71f92bd3: 139/260 surviving requests byte-identical; all other 121 requests / 140 modules equivalent with retired SpecIds 1017-1019 fixed at 0, other constants preserved (shared spec_cascade_check canonical comparator).
- Both sides: 297/297 module entries validated, zero errors; retired IDs never reused, ABI unchanged.
- shaders/variants.txt: 165 -> 165 requests (delta 0); total unique requests 261 -> 260 only because rt/hair_mean.hlsl was deleted.
- Defaults regenerated and checked; retired keys absent from defaults/GUI/schema; legacy INIs with all four keys 0 or 1 load unchanged in every preset (settings_test: 3,865 checks, 0 failed).
- Full build passed; 145/145 CTests passed in 154.70 s, BelowNormal and serial; VS Installer directory added to PATH for vcvars discovery.
- Evidence: build/hair-retire-identity.json, build/hair-retire-identity-current.log, build/hair-retire-final-build-test.log. No game launch, commit or git metadata writes; mean device-loss cause remains unknown.
- Bundle10 22fd8ad008cc: resolved conflicts; kept surviving GUI review changes and water Fresnel cache key; regenerated all five hair-validation jobs/plans.
- Retirement identity: 139/260 requests byte-identical; other 121 requests / 140 modules equivalent with only retired SpecIds 1017-1019 fixed at 0; 297 entries validated per side, zero errors. Variants 165 -> 165 (delta 0).
- Full build passed; regenerated defaults/schema; settings 4,103 checks passed; live_flow hair-validation lint 5/5 passed. No game launch or Git metadata write commands.
- CTests: 146/147 passed in 353.04 s; water_surface_shader_test fails raw identity against old merge-base 71f92bd3 (1/16 arithmetic-OFF, 1/2 ordinary identical; 16/16 production validated), which retains retired hair constants. Gate left unchanged; logs: build/hair-retire-bundle10-*.log.
- Water gate now accepts raw differences only through the shared retired-zero comparator, preserving the same OFF reference and all surviving constants; 59 regression checks passed. Per-request paths: build/hair-retire-water-surface.json (OFF 1 raw + 15 equivalent; ordinary 1 raw + 1 equivalent; production 16/16 valid).
- Main ea0674e0 retirement passed: 139/260 raw-identical; remaining 121 requests / 140 modules equivalent; 297 modules validated per side, 0 errors. Evidence: build/hair-retire-ea0674e0-identity.log; variants delta 0.
- Full build + 148/148 CTests passed (562.24 s; build/hair-retire-gate-build-test.log). live_flow: hair_validation 5/5, hair_crowd 2/2; hair_primary 0/1 (existing forbid, foreground FPS and warmup issues). No game launch, commit or git metadata writes.

## Launcher GUI review fixes (branch gui-review-fixes, offline verification, 2026-11-03)
- Fixed all 13 review findings; strand primary activation is latched at device startup and requires restart.
- Hair capability/local groom checks, startup prerequisites and Experimental authorization are enforced; unqualified controls stay in Advanced and off in presets.
- Added water Combined (7) and shading prerequisites; crowd labels use segment budgets; FG badges follow Off/on transitions.
- Disclosures and report focus survive refresh; sliders expose Off and units; card copy and costs use central metadata with Details.
- Validation: full build and 146/146 CTests passed (194.04 s); settings 4,356 checks, bridge 904; stale count corrected; defaults/schema and diff checks pass.
- Kept; ABI and shader request delta 0. No game launch or Git metadata writes; WebView2 zoom, high contrast, Narrator and DPI acceptance remain manual.

## Hair groom CPU conversion (branch codex/hair-groom-speed, kept, 2026-11-03)

- Vectorised mapped runs and exact segment medians; reused triangle Jacobians/norms and batched UV searches.
  Dedupe uses scalar pair keys and early other-card filtering; neighbour lookup searches occupied cells once.
- Every field and dropped count matches the real-style references bit for bit; max absolute/relative diff 0.
  Shadowheart: 85,667 strands, 529 dropped; Gale beard: 21,608 strands, 2,838 dropped;
  Halsin: 142,002 strands, 6,552 dropped.
- Convert/dedupe seconds, supplied baseline -> measured: Shadowheart 8.4/5.9 -> 3.253/1.431 (3.05x total);
  Gale beard 4.8/4.0 -> 2.824/0.898 (2.36x); Halsin 12.6/12.5 -> 4.475/2.834 (3.43x, target >= 3x).
- Independent final `tests/hair_groom_test.py`: exit 0, `all passed`, 5.06 s;
  `tools/hair_groom/speed_check.py <inputs dir>` exits 0.
  Python ran BelowNormal; `python.exe` substituted for unavailable `py -3`. Baselines were supplied,
  not rerun. No game/GPU tools, commits or Git metadata writes; no defaults, thresholds or format changes.
- Offline check: run the two commands above with the saved real-style fixtures. No in-game check is needed
  for this CPU-only, output-identical optimisation. Shader variant request delta: 0.

## Strand fail-safe strike rule (branch claude/hair-strike-rule, 2026-11-03)
- hv5 evidence: every strike event struck every groom of its counted frame at once (5 of 5 at 20:45:32 and 20:46:17,
  3-4 in warm-up); one groom shadowing another cannot do that (it hides only a fully covered groom). The code allows a
  frame misalignment: the build (and publish) is recorded at the end of the prepass, the G-buffer in another pass the
  game may submit in another order, so the record carries an adjacent frame's tag and the count sees 0 for all.
- Fix: the count pass also counts strand entries sealed with tag - 1 / tag + 1; a frame with only those is no
  evidence (logged "carry an adjacent frame's tag"). Grooms sharing a draw key (same hairstyle twice) are summed.
  A strike retires a draw for 600 frames (doubling to 9600), not for the session. Unit tests in hair_groom_asset_test.
  predicted.json (hair_validation, hair_crowd) names what confirms or refutes the hypothesis in one run.

## Hair reload fail-safe (branch claude/hair-reload-fix, 2026-11-03, after bundle7)
- Device lost 2026-11-03 19:53:28 (user, strand hair turned on live, main 71f92bd3): the faulting read 0x23c4e00000 is
  mesh pool [0x22cae00000, +2 GB) + 0xFA000000, inside the 4 GB a 32-bit offset reaches from the pool's BDA; the
  registry named it "old soup" only because the retired 2047 MB soup (elastic shrink 19:53:06) occupied that VA and
  was never untracked. Readers: the strand G-buffer, velocity and fibre passes load pool + row offset + i0 * 12 with
  i0 read from the pool (32-bit, unchecked). Fix: header word 14 = pool size; hp_segment checks every pool byte a
  reader touches for the segment (64-bit offsets) and fails closed; the retired soup leaves the fault registry.
  Python twin + the crash's numbers in hair_groom_test. Identity 257/262 (only key-on shaders). Not run live; the
  source of the bad i0 is not identified (the bound makes it harmless).

## Strand header in the G-buffer's command order (branch claude/hair-publish-order, from 51956f86, 2026-11-03)
- Key rt_hair_groom_primary_order (off, live): the groom header is staged at the build (prepass command buffer) and
  applied right before the main G-buffer pass in its own command buffer: the newest staged publish whose build is in
  the same command buffer or already submitted (hair_primary::pick_publish, unit-tested); none: no strand swap that
  frame, the next prepass keeps its card depth. G-buffer and velocity swaps need the draw in the applied groom set.
  The counts pair with the applied tag. Layer only, no shader change. Build, 147/147 CTests. Not run live: use it if
  the confirming run shows "carry an adjacent frame's tag" lines (A/B: toggle the key live, expect those lines to stop
  and strand draws > 0 in every strand window).
- Knob exp_hair_primary_order (Developer, dev_only, Live, off in every preset). The 300-frame counted-frame line now
  reports the counted frames with adjacent-tag pixels and the header order. hv5 gains the header-order A/B after its
  stages: order-prepass-1/2 (key 0: adjacent-tag frames observed, not gated) and order-gbuffer-1/2 (key 1: no
  adjacent-tag frame in -2, gated), strand draws gated in all four; predicted.json has the decision. live_flow 5 jobs 0
  failing; 147/147 CTests.

## Strand hair diagnostics (branch claude/hair-primary-diag, 2026-11-03)
- hv5 (main a8382760) was valid but its primary stage drew no strand: after the stage-d reload every draw struck
  ("its strands drew no pixel in 2 frames"), 0 G-buffer strand draws in every 300-frame line. Cause unknown.
- Now: the 300-frame line "[hair primary] counted frame F: strand pixels per groom (LOD, draw key prefix): ...;
  pool-bound refusals N (session M)"; each strike names its groom k, LOD and every groom's pixels of the counted
  frame; the count pass counts the strand pixels whose segment fails the pool bound (hp_segment), so it is no
  longer silent. hv5's primary survival windows (25 s) and hv4's after-reload window now need strand draws > 0.
  Identity: only rt/hair_primary_count.hlsl differs (key-on). live_flow 5 jobs 0 failing; the hair_validation
  dryrun fails on every job (also unchanged hv1-hv3: the lean_dryrun rules on main are stricter than these jobs).

## Hair live jobs for the latched strand key (branch claude/hair-jobs-latch, 2026-11-03)
- main 8df7978d latches rt_hair_groom_primary at device creation (no live enabling for users). The hair jobs now set
  it at startup (startup_only in live_flow) and A/B with rt_hair_groom live: off = native cards everywhere, on =
  strands in RT and on screen. hair_crowd lost its RT-only window (no live mode for it any more). hv5's stages:
  native, + physical origin, + strands (groom), + fibre pass; both strand stages need strand draws in their 25 s
  survival windows. Warm-ups are never judged (hc1: a build gate on warm-back stopped the run).
- live_flow: hair_crowd 2 jobs 0 failing (dryrun passes, 11.7 min, 1.8 GB), hair_validation 5 jobs 0 failing.
  hair_validation's lean dryrun still flags phases below 5 s that main's tools add (tighten's 1.1 s restore,
  producer-sanity 1.1 s phases): a conflict between live_flow tighten and lean_dryrun, not these jobs.

## Hair batch (branch claude/hair-batch-lod, 2026-11-03, after bundle8)
- Game use (tools.hair_groom.reach): of the 698 unconverted styles 130 are unreachable, 568 reachable (150 LOD1 of a
  passed LOD0). Fixes: zero radii from collapsed card triangles, mirrored UVs reviewed against the coverage bound;
  the 200 affected reachable styles rerun: all pass. Now 719 pass, 372 review, 126 reject (368 reachable still
  unconverted). Table and per-class causes in docs/RTX_HAIR_PRIMARY.md 5a.
- Companions (2026-11-03): every companion head hair passes; Halsin passes after the orientation test counts only
  radial strands (26 styles more). Gale's beard and the short beards stay in review (systematic, see RTX_HAIR_PRIMARY
  5a). 745 pass, 346 review, 126 reject.
- M1 conversion speed (Codex, verified): converter 2.4-3.4x and renders 1.8x faster, bit-identical on Shadowheart,
  Gale's beard and Halsin; GR2/groom caches with --revalidate; representative set of 80 styles. LOD1 transfer from
  the LOD0 groom rejected by its gates (LOD1 cards are thinner than LOD0 by design).

## Elastic demand headroom (branch cascade-elastic-headroom, READY_FOR_LIVE, 2026-11-03)
- Existing rt_capture_elastic: proactive cascade growth; RT/cascade reserve uses preset quarter reserve and measured positive demand steps over retirement + producer delay; falling cap cannot turn growth into shrink.
- Synthetic S8 envelope 206.18 -> 301.71 MiB fits before growth: 320 MiB static, 592 MiB after step, 320 MiB after 120 quiet samples; oscillation retains reserve.
- Three slots: static 960 MiB (+144 vs 272/slot, -321 vs fixed 427/slot); post-step 1776 MiB (+672 vs 368/slot); temporary old+new overlap up to 2736 MiB in this trace. RT fixed floor retained.
- Layer build and 2/2 capture CTests pass; synthetic step/oscillation/quiet/delay/cap tests. No game, commit, shader/ABI/key changes (variant delta 0). GPU cost unmeasured; layer restart required.
- Expected live: every su1 S8 Q/E/rotation window has cascade buffer full 0.0 and RT opaque buffer full 0.00. Unseen larger steps, stalled fences or refused allocations remain unbounded; not a live pass.

## Pak publication audit (pakjoin-publish, offline; not qualified)
- cp4 ON frames 2280/2520: exact/records/ready 0/0/0; witness 21,588 means rejections, not full joins; startup key worked, basis is downstream.
- Sampled-only readback lacks full raw object-space witnesses; no safe publication shortcut. Native rows already serve 195/309 pool draws, 259/473 instances, 199 ready at frame 2520.
- Log/catalogue audit: 286 readbacks = 46 unique/229 missing/11 ambiguous; all 229 misses have count candidates (UV mismatch). Aggregate missing 91.39% of attempts, 58.98% of probed draws; family/pixel shares unidentifiable.
- inputs_pending 166,194 is cumulative unavailable draws, not backlog; readbacks cap at 16 jobs/16 MiB. Fix terminal absent-count draws being counted as waiting; add explicit missing_count/missing_uv and rejection labels.
- Predict ON exact/records/ready 0/0/0 with sampled-only inputs; attempts + inputs_pending = draws; missing_count + missing_uv = missing. Missing-count transfer size unknown; no positive publication prediction justified.
- Host build, 6/6 focused CTests, real 40,983-row parser and diff check pass (settings binary rebuilt after stale-binary failure); ABI/shader request delta 0, key/presets unchanged; no game, commit or GPU cost claim. Full raw witness/decoding remains required.
- Evidence: tools/live_batches/cov_paks_compact/publish-audit.json; design: TRACK_D_REFLECTIONS.md. pakjoin-zero entry read from evening archive (standalone file absent).
## Full raw witness follow-up (pakjoin-publish, READY_FOR_LIVE)
- Unique UV candidates now stage raw index/position/half2 or float2 UV copies through existing retirement; ordered SHA match feeds unchanged recipe/resource/cull/basis gates.
- Bounds: 4 MiB transfers/frame, 16 jobs/16 MiB in flight, 2 raw retirements/frame; chunk large ranges, reject oversize/mapped mutable inputs, invalidate dependent records on reuse/writes.
- Host build and 7/7 focused CTests pass: 88-byte candidate -> raw decode -> SHA -> publication -> hit lookup, unsampled mismatch, budget retry, chunk discard and reuse invalidation.
- cp4 predicts witness_ok > 0, published_records > 0 and basis ready > 0 in both ON windows; live_flow preflight passes (1 job, 0 failures).
- rt_hit_material_paks stays off in presets; ABI/shader request delta 0. No game launch, commit, measured coverage or GPU cost.

## Evidence and reading order

- [Full folded progress, verbatim](archive/PROGRESS_20261103_evening.md) preserves all prior entries.
- [Handoff](HANDOFF_SESSION.md) gives the queue; [GOAL_NEXT](GOAL_NEXT.md) defines the stages.
- [18:56 evidence](../tools/live_batches/combined_20261103/RESULT_WATER_185612.md) supplies the exploratory costs.
- That combined run had all six slices INVALID. Repairs do not retroactively validate its captures.
- Current user steering adds the stale-soup crash diagnosis and DoF appearance report, absent from the folded text.
- The stale soup address follows elastic capture shrink; its fix is pending on claude/hair-reload-fix.
- The local branch ref does not yet expose that new fix. Earlier reload safeguards are already merged.
- Later verdicts here supersede stale READY_FOR_LIVE and unmeasured labels in the archive.

## Stage 2 - Shader specialization: done

- Migration merged and scc1 valid: 0 DXC compiles on converted toggles.
- Requests fell 2,946 -> 147 before null-key cleanup, then 147 -> 135.
- Later half-trace layouts added 18 requests; these are separate manifest snapshots, not current totals.
- Bundle 6 final shader gate: 262/262 requests, 28 hit-shade interfaces, 0 failures.
- Full-resolution identity: 243/243 requests byte-identical to the bundle baseline.
- Qualified infrastructure needs no new live job. New on/off keys use specialization constants unless layouts change.
- Open: first-use pipeline continuity and toggle latency need live confirmation; no blanket latency pass.

## Stage 3 - Visible defects: fixes implemented, water qualification open

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving shadow band fixed by default.
- Motion-pair band is within RT-sun-off noise. Check Test Dialogue3 while the camera moves.
- User accepts AO self-occlusion: 50% of character pixels, 99% self-hits below 5 cm. No correction planned.
- Rejected: rt_cascade_static_only drops every caster. Keep it off.
- rt_water_compose_shift uses matching camera fields; offline static delta is 0. User says it looks right.
- READY_FOR_LIVE: tools/live_batches/water_flicker_wf9/jobs/wf9-s8-compose-shift.json.
- Require static average camera delta < 0.01 m, nonzero samples, corrected coverage and held Q/E windows.
- Exploratory corrected compose cost is about +0.009 ms; reflection residual and energy remain unqualified.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa. Do not restore their old jobs.
- Open: reflection-only rt_water_rf_edge and rt_water_rr_hitdist remain unknown/off.
- Whole-water glint scores cannot qualify reflection quality. Never compare pans from separate runs.

## Stage 4 - Qualification: partly done; hair safety blocks resumption

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow stays 1. Earlier integrated smoke passed; fresh smoke must follow the crash fix.
- OMM camp: 1.17 billion rays, 0 untested, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms.
- Box S6: signed energy difference 0.0 within noise; unshadowed traces 369 million -> 0 per 120 frames.
- Box local-light pass 0.45 -> 0.58 ms (+0.13 ms). Inspect S6 Grymforge and camp art lights.
- Pool alpha rows: S6 qualified coverage 14.8% -> 52.1%; energy +1.06%/-0.38%; GI trace +0.46 ms.
- Wind RGB: S8 coverage 45.0% -> 63.5%, energy -0.00%, trace +0.67 ms; kept in Ultra.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Converted secondary grooms work in game, about 40% cheaper than native cards in prior trials.
- Dialogue1: RT 9.07 vs cards 10.2-10.57 ms; GI 12.51 vs 14.1-14.3 ms; refit +0.57 ms.
- Native cards stay off: +2.3-6.3 ms RT. GI culling was rejected for brightening by +9-20%.
- Batch: 519 pass, 553 review and 145 reject out of 1,217; 698 unresolved = 553 + 145.
- Strand primary/fibre/crowd are not qualified. Do not enable strand hair before the pending crash fix lands.
- Blocked live regression: tools/live_batches/hair_validation/jobs/hv5-reload-bisect.json, Test Dialogue1.
- Then smoke: tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json with the real qualified preset.
- rr_post_temporal is READY_FOR_LIVE/off; user sees steadier DoF, not yet a promotion gate.
- Exact next job: tools/live_batches/post_temporal/jobs/pt-dialogue3.json, Ultra 0/1/0/1.
- S8 temporal reports 0.127/0.119 ms (~0.12 ms); its shot had no DoF composite and cannot qualify Dialogue3.
- History is about 250 MiB at 2560 x 1067. Both ON windows need hair-over-DoF quality and float energy gates.
- Rejected by user: rr_hair_resolve and CPU perf keys. No further qualification queue for them.
- IP grass stays off: only +3 coverage points for +1.9 ms and about +1.5 GB; no promotion.
- READY_FOR_LIVE generated rows: tools/live_batches/m4_generated/jobs/hgn1-generated-camp-fg2.json
  and tools/live_batches/m4_generated/jobs/hgn2-generated-inn-fg2.json; publication race fixed, recovery unmeasured.
- GI guards remain pending in tools/live_batches/qual_stage4/jobs/01-camp.json and tools/live_batches/qual_stage4/jobs/02-inn.json.
- Open: hair runtime mask/MV/fresh/FG/PT evidence and cohort coverage remain incomplete; dryruns cannot qualify hair.

## Stage 5 - Effects and performance: active, candidates remain experimental

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far energy: S8 -2.01% vs -2.0% prediction, camp -2.42%; trace cost +3.2/+3.7 ms respectively.
- Earlier water chain cost 0.53-0.67 ms; hidden-hit emission +0.08 ms. These are historical scene costs.
- rt_water_hit_shade remains off/experimental; retained lighting must have proven lifetime for F-1 use.
- rt_water_hit_cost bits: 1 sparse, 2 cone/zero, 4 slot order, 7 combined. All remain off in presets.
- Sparse reflection median 2.107 -> 0.530 ms; repeat 2.491 -> 0.531 ms. Energy/coverage gates did not pass.
- Combined reflection about 2.1 -> 0.529 ms; compose about 3.5 -> 1.0 ms. No equivalence claim.
- Bits 2 and 4 show no consistent independent saving. Keep the same-run per-bit comparisons.
- READY_FOR_LIVE quality job: tools/live_batches/water_offscreen/jobs/wo1-s8-hit-shade.json.
- Require post-water float energy within OFF-repeat noise, preserved coverage and zero world truncation in every ON.
- rt_water_surface_fresnel is READY_FOR_LIVE/off; restart to provision, then live A/B in S8.
- Exact job: tools/live_batches/water_surface_fresnel/jobs/wsf1-s8-interface-column.json.
- Offline: 144/144 CTests; 2/2 ordinary raw requests identical, 16/16 compose arithmetic-OFF comparisons identical.
- Inspect shallow pool-rim reflection and interface/column partition. Shading-model brightness may change; cost unmeasured.
- Pak discovery fixed: class-A keyed draws now participate; rt_hit_material_paks stays off pending camp live evidence.
- Exact job: tools/live_batches/cov_paks_compact/jobs/cp4-compact-camp-join.json after a layer restart.
- Compact v4 has 40,983 rows/40.01 MiB. Predict draws delta 36,720-36,960 per 120 frames; UV attempts must be nonzero.
- Full witness/recipe/resource/cull/basis gates remain mandatory; retained camp has 58 unique UV candidates, 0 full witnesses.
- The old LSLib generation blocker is superseded by compact catalogue validation; publication/texture-view gaps remain.
- rt_trace_scale=0.5 is configured for Low/Medium; High/Ultra retain 1.0. Live quality is not qualified.
- Medium exploratory RT 1.040/0.864/1.057 ms: about -0.18 ms; shot count failed, energy evidence incomplete.
- READY_FOR_LIVE: tools/live_batches/gpu_opt2/jobs/go2-s8-medium.json and tools/live_batches/gpu_opt2/jobs/go2-camp-high.json.
- Exact GPU candidates stay off: rt_compact_hit_state, rt_alpha_metadata_cache, rt_water_empty_tiles.
- Offline replay 36/36 byte-exact comparisons. Empty tiles saved about 0.156 ms compose exploratorily; no live equivalence pass.
- READY_FOR_LIVE: tools/live_batches/gpu_opt1/jobs/go1-camp.json and tools/live_batches/gpu_opt1/jobs/go2-s8.json; retain cutouts/colour.
- rf_hit_shade remains experimental: +1.4-1.8 ms; rf_hit_gi needs a pack with instanced hits for its AO gap.
- SHaRC is inconclusive: no cache history/PT superiority evidence; sharc=0 remains. No promotion job.
- Historical SHaRC energy -0.86% vs +1.5% prediction is INCOMPLETE; missing dumps prevent requalification.
- One GI ray saved 4.32 -> 2.02 ms with +0.11% energy, but flicker null failed. Ultra retains two rays.
- Open defects: animated lava, Tileset colour, HeightGradient height, HLOD two-sided, fibre scattering and AS fragmentation.

## Stage 6 - Tooling and release: offline repairs merged, acceptance open

- Main contains fail-fast, preflight lint, 15-minute cap and durations derived from cadence/readback/settle time.
- Main also contains combine rebasing, session fault invalidation and interrupted merge-green recovery.
- Post-core water energy and published-stride coverage capture fixes are merged; restart and validate on first use.
- Failed shader requests no longer enter the prefetch manifest. Preserve real failure gates.
- Latest duration repair passed 146/146 CTests; first live use must confirm duration and fail-fast behaviour.
- Do not reuse oversized combined drafts. Re-lint actual jobs and inspect waits/loading before authorization.
- Launcher fixes cover preset reset, prerequisites, Apply badges, focus and contrast; bridge passed 695 checks.
- Open acceptance: real WebView2 zoom/high contrast/Narrator, disposable-VM installer, long sessions, acts 2-3, other GPUs.
- Packaging/licence decisions remain open. Game data is generated locally; historical packaging receipts are not approval.

## Stage 7 - Backlog and cleanup: ordered, not a rendering pass

- Preserve pending hair-cards-retire, anim-materials and claude/hair-reload-fix work; see the handoff dependencies.
- Audit rejected keys, generated-row supersession and remaining READY_FOR_LIVE jobs after the ordered queue.
- Keep S8 ambient-ratio 1.44 question, inn records, dynamic/cascade BLAS relocation and M5.5 follow-ups open.
- Retain current replay/gate inputs; cleanup must respect shared junctions. No cleanup is performed by this consolidation.
- This edit archives the full folded text and replaces only progress/handoff. No code, shader, game or Git metadata writes.
