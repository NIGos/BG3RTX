# BG3RTX progress archive - 2026-11-01 evening (verbatim)

The full PROGRESS_CODEX.md as it stood at main d089b7b (17 fragments folded), before the ~150-line rewrite.
Append-only history; the current summary is in [../PROGRESS_CODEX.md](../PROGRESS_CODEX.md).

---

# BG3RTX progress - 2026-11-01 (main f148833, ABI 25)

One current summary. Older entries, verbatim: [archive/PROGRESS_20261101.md](archive/PROGRESS_20261101.md)
(11-01 until the afternoon rewrite), [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md),
[archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md). Branches write
`docs/progress/<branch>.md`; `py -3 scripts/fold_progress.py` folds them here. Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).

## Open questions for the user

- **`rf_hit_shade` bias.** Tier (a) L_b / L_a is 0.926 at S8 (-7%) and 1.091 at S6 (+9%). The strict M4.2 gate
  fails (any bias is significant with 10^7 hits). Accept "bias within 9%" for Ultra, or keep it out of the
  bundle? Conservative default: out of Ultra until you decide. Its cost is also +1.4 to +1.8 ms (rf pass).
- **`rt_hit_emission` 1 or 2** (not blocking): 1 = hidden hits only, 2 = also screen hits (the screen value has
  no emission). Conservative default: 1.
- Answered: **`ls_k` stays off** (user decision, 11-01).

## Long tools monitorable and cached; capacity gate lint; smoke_ultra job (branch worktree-agent-afcf4887c24de6c43, offline, kept, 2026-11-01)

- Shared helpers: `tools/longrun.py` (Progress every 30 s to stderr: done/total, rate, elapsed, ETA, failures;
  FileCache keyed by mtime+size in the main checkout's `captures/.cache/`) and `scripts/longrun.ps1` (step
  start/end lines with durations, a heartbeat for captured output, a step table, content-hash reuse).
- Before -> after (typical input, BelowNormal):
  - merge-green: silent until each step ended -> step start/end/duration lines, a heartbeat every 30 s of output,
    each step's lines live in `build\merge-green-<step>.log`, a step table at the end.
  - build.ps1 -Test (4 min 56 s fresh: build ~3 min, ctest 122 s): ctest streamed with build/ctest durations and
    the 5 slowest tests (pt_reference_test 52 s, frame_replay_test 23 s, transport_bsdf_test 6.6 s).
  - frame_replay PT / native ambient / --repeat: silent -> `[replay] progress` every 30 s (1000-iteration PT on a
    synthetic pack: 156 s, five lines with ETA). post_replay.ps1 shows those lines live and times both runs.
  - lean_dryrun (box_lights 14 s, consolidation 37 s): run A/B start/end and per-job record lines; unchanged
    inputs (content hash of the batch, scripts\*.ps1/*.py, parameters) reuse the last pass: 14.3 s -> 0.8 s
    (`-Force` reruns). Output lines identical.
  - m4_promotion_gate (all m4_promotion logs): 5.1 s -> 2.7 s cold / 1.0 s warm; git show/log memoised and cached
    (144 git calls were most of it). Stdout byte-identical. m5 gate and queue_results.py: about 1 s, unchanged.
  - material_families: 70 s -> 3.7 s on a rerun (world count cached by the .pak signatures; per-trace frame
    evidence cached). Rows identical; their tie order varied run to run before this change too.
  - scripts.qa: the game run (minutes, output captured) prints a heartbeat every 30 s with traces written;
    analyze prints scene n/N with ETA. Not run here (needs the game; no QA captures on disk).
- queue_gates.py (on top of the merged scene-wait lint, branch worktree-agent-a498dc85e3606abe9): CAPACITY_GATES
  (RT capture opaque drops 0.00, cascade buffer full 0.0 where rt_cascade_casters=1) and `--lint-capacity`, judged
  like the scene-wait lint over traced cost windows; unit tests in queue_gates_test. 39 job problems are listed, not
  fixed (job files left alone to avoid conflicts): apply after the scene-wait branch merges, then make it a dryrun Check.
- New batch `tools/live_batches/smoke_ultra` (su1, S8, Ultra + every qualified experimental key, static / pan /
  rotation, water traced > 0 in every window): dryrun passes; expected to FAIL live on the water gate until water
  runs with rt_cascade_casters=1. READY_FOR_LIVE, not queued.

## Replay packs carry the cascade casters; camp / inn ambient batch ready (branch worktree-agent-af9746e0d070a8c33, offline, READY_FOR_LIVE, 2026-11-01)

- Writer fix: the rt_cascade_casters instance went into packs with no triangles (InstInfo tris 0, replayed with
  mask 0), so the replay TLAS had no roofs. bvhsplit.cpp now records each ring slot's cascade instance and soup;
  layer/replay.cpp copies that soup (`casters.bin`, manifest `casters FILE INSTANCE TRIS STRIDE`) while its ring
  slot still holds the same frame, else notes it left out. frame_replay appends it to the soup, points the
  InstInfo entry there (`rp::caster_patch`) and builds its BLAS as live (not opaque, mask 0x02); `--no-casters`
  for A/B. Expected size: 52 MB at camp, 84 MB at the inn (2026-10-31 cascade logs).
- Second writer fix: core passed `defines=""`, so a casters-on pack replayed GI rays at 0xFF instead of the live
  0x01. Now the running variant's defines go in, filtered to the contract (`rp::contract_defines`: RT_CASCADE_MASKS;
  the others noted). Old packs replay unchanged (S8 142819-1: bit-equal 99.5738%, same counters).
- PT: `PT_CASTERS=1` (auto for a casters pack) traces the cut-away roofs as triangles (shadow rays with the live
  0.5 m / 2% near rule; path rays get a fallback-material vertex). `--native-ambient` prints a new `PT split` line
  (far-field share on / off screen, live domain, sun / local). `tools/ambient_table.py` prints the per-scene row.
- Tests: replay_pack_test (casters directive, patch, defines), pt_reference_test 8-10 (casters box closes at
  exactly 1 / 1.75; open without PT_CASTERS), ambient_table_test; all 82 CTests pass. replay_pack=0 is untouched
  (the layer keeps one small record per TLAS build).
- Live batch (not run): tools/live_batches/replay_capture/ambient, `rc2-ambient-camp-fg2` and `rc3-ambient-inn-fg2`
  (casters on, exposure held, cap 3500 MB, post step runs the replay, native ambient and table, keeps the newest pack
  per scene). Dry run passed: about 5.5 min, up to 7 GB (two packs of about 3.1 GB). Needs a layer + core build.

## Water on Ultra (rt_cascade_casters) and capture sizes for wide vertices (branch worktree-agent-ae9b318078664bcac, READY_FOR_LIVE, 2026-11-01)
- Bug 1: core/water.cpp skipped every water pass while `rt_cascade_casters` (Ultra) was on: user log
  `[water] reflections (300 frames, 0 traced)`, `views 0`. Now the water reflect/compose pipelines build
  the cascade requests (common/shader_variant.h `water_request`): `RT_CASCADE_MASKS=1` (reflection,
  refraction and local-light rays trace 0x01, as core/gi.cpp's cascade variants) and, with WATER_SHADOWS,
  `RT_CASCADE_SUN_HIT=1` (only the water sun ray sees the roof casters, as gi_hit_sun at a GI hit).
  The key is read live like gi.cpp; a pipeline of the other request never runs. The skip stays (counted)
  for rt_omm, rt_native_tileset, rt_native_scalp, rt_native_hair, rt_hair_physical_origin, logged once
  per change: `[water] reflections skipped: no water variant for ...`.
- Shaders: 5 new requests in shaders/variants.txt, all compile; the 4 existing compose requests
  (water_copy and water_compose) are byte-identical SPIR-V to main; cascade variants have the same
  descriptor bindings and push size as the plain ones (spirv-cross reflect).
- m5_water (5 jobs) and m5_promotion (2 jobs) now run with `rt_cascade_casters=1`; they expect the
  `... RT_CASCADE_MASKS=1 water_reflect: built` (and compose) lines and forbid the variant skip.
- Bug 2: with rt_hit_material the capture vertex is 64 B, not 24 B, but Ultra kept rt_xfb_mb 384 and
  rt_cascade_mb 160 (user run: `opaque peak 510.6 MB of a 384 MB buffer`, cascade `buffer full 86-132`).
  A cap left at its default now follows the startup stride, rounded: 384 -> 1024 and 160 -> 427 at 64 B
  (88 B hair: 1408 / 587, the sizes the hair jobs set by hand). Any other value is used as set.
  common/rt_capture_budget.h `effective_mb`, unit tested in rt_capture_budget_test. Logged:
  `[geo] RT capture buffer N MB (rt_xfb_mb S, B-byte vertices)` and `[cascade] capture buffer N MB (...)`.
  The cascade stats line now prints the effective size (and the stale literal 64 is gone).
- Cost: none measured yet (water now runs on Ultra; its passes cost what the m5 jobs measure). Memory:
  +640 MB RT capture and +267 MB per cascade capture buffer with rt_hit_material at default caps. Layer change: game restart.
- User check: Ultra in S8 Water Queens House: the water reflects, and `[water] reflections (... N traced)`
  has N > 0; with rt_hit_material, no `opaque draws dropped` and cascade `buffer full 0`.

## gi_far live +2.9% was the eye adaptation; energy gate now exposure-normalised (branch worktree-agent-a8879277994ee06b3, offline, 2026-11-01)

- Stage found: none of ours. The lighting is pre-exposed by s1b13[0]; in the gf2 ON shot the adaptation had raised it
  3.2% (EV 11.413 -> 11.367; camp +4.1%). The ON / OFF change is a uniform factor (R / G / B +2.91 / 2.93 / 2.94%,
  16 x 9 tiles median 1.030 with the OFF-OFF noise width). The gf jobs had no `qa_exposure_hold`.
- Derivation (docs/TRACK_C_GI.md "Live energy: the eye adaptation"): temporal (RR pass-through), gather, AO fold,
  SSS and compose are linear; the ao_direct give-back makes traced direct light independent of the folded AO. With
  a lower visibility only the bounce can add light, so no GI stage can flip -2% to +2.9%.
- Normalised by the trace frame's s1b13[0]: S8 scene colour -0.32%, diffuse -0.26% (OFF spread 0.21%), camp -0.14%
  (0.11%). The sign now agrees with the offline -2.0%; the size does not (open, see the next run).
- Fix of the test: `tools/m4_promotion_gate.py` divides scene colour by the `[rrpost] exposure probe` value of each
  trace frame (raw change and exposure ratio printed beside it) and prints a "Stages" table; CTest
  m4_promotion_gate_test (new in the CTest list). m4_promotion runs barely move (camp shot-on +0.25% normalised).
- `tools/live_batches/gi_far`: both plans hold the exposure from cost-off-a; gf2 (S8, one launch, 157 s) adds the
  stage windows: `diag-ls-off/on` (ao_direct give-back), `view-vis-off/on` (gi_debug=3, folded visibility, predicted
  ON / OFF 0.948), `view-bounce-off/on` (gi_debug=2, about 1.6). Dry run: all checks passed, 8.5 min, 0.6 GB.
  NOT authorized. No shader, core or default-path change.
- gi_far stays off. Next: run gf1 + gf2 when the user steers a batch; read `py -3 -m tools.m4_promotion_gate`.

## Hit-material blockers ranked; wind RGB 2S foliage rows (branch worktree-agent-a87dc09be59810f7c, offline, READY_FOR_LIVE, 2026-11-01)

- New offline ranking: `frame_replay --define HM_BLOCKERS=1` counts each lookup per TLAS instance (raw slots 448+,
  replay only; `rtstats.bin`), `tools/hit_material_blockers.py` joins them with the pack (InstInfo class, alpha
  record FS, geometry). S8 pack (rc1, instanced off, 280660 lookups, 52.8% qualified): pool meshes without a row
  14.9%; soup alpha without a row 25.4%, of it depth FS 0d91554d 13.3%, bfbe4747 (2S, already built) 5.7%,
  e62cdef4 (IP bushes) 2.2%, e2ecf65a (static Base_AlphaTest family) 2.1%; soup without record 5.8% (characters
  5.0%, terrain 0.8%); stale opaque rows 1.0%.
- Asset DB depth variants: 0d91554d = ATM_Wind_Vertex_RGB_AlphaTest_2S templates; at S8 and camp its triangles are
  735d5a19/83831811 (Translucency). Camp and S6 have no pack: camp row 51.6% is soup alpha (by triangles grass
  73849471 -> 105e47e4/35a18400 67%, IP bushes 13%, wind RGB 6.6%); S6 base 65% is soup without record (lava
  bd5dbda2/0b8dfd73, terrain 64e25689, characters, hair) since 94% of its pool instances qualify.
- Built (off): `rt_hit_material_wind_rgb` (startup, GUI "Native hit materials: RGB wind foliage", needs the
  instanced rows): third instanced family, depth 633929ef/0d91554d, G-buffer 735d5a19/83831811. Recipe = 14678345
  with the two-sided N, reflectance 1:1 byte 20 (classifier terms pinned in material_families_test); class 0 front
  lobe only (no leaf transmission). Bit-exact clip check (no tolerance), pairing as wind + 1:1 _m4, counters
  427..434 (`hit material instanced clip wind RGB 2S`, renumbered on counter-renumber after rf_hit_gi 419..426).
- Unchanged with the key off: `instanced::gate` hides the family from every lookup (native_hit_instanced_test);
  shader identity gate PENDING (run the new cached tools/shader_identity.py: every request without RT_HIT_INSTANCED must be byte-identical; HM_BLOCKERS and the family-3 counters are under defines).
- Tests: build + 82 CTests pass (settings schema regenerated); material_families_test 15 (3 new);
  native_hit_instanced_test and native_vt_recipes_test extended.
- Expected qualified share: S8 up to +13 points (minus clip rejects), camp about +3 to +7, S6 0 (absent).
- Live (not authorized): `tools/live_batches/m4_wind_rgb` (S8, 144 s, exposure held in every A/B window,
  expect_every on mapped rows and clip rejects < 0.04%); dry run passes. Cost not measured.
- Next blockers: pool meshes without a row at S8 (14.9%, families not in the pack), camp grass 105e47e4/35a18400
  (VARIANT + distance-dither discard, wind VS), S6 lava and terrain recipes, characters.

## Energy recheck at a fixed exposure (branch worktree-agent-a80bee797afaacedd, offline, 2026-11-01)

- Merged 17e82e1 (exposure-normalised gate) and 9629658 (gate audit: two-sided band, plan recovery);
  `tools/m4_promotion_gate.py` now does both: scene colour / s1b13[0] first (energy and OFF-OFF noise pairs),
  then the two-sided band. Table: docs/GATE_AUDIT_20261101.md section 7.
- No run today held the eye adaptation. Verdicts that change: gf1 camp +3.97% -> -0.14% (FAIL -> within);
  gf2 S8 +2.87% -> -0.32% (sign of the offline -2.0%, still FAIL vs -2.0%); m5w4 +2.75% / +2.20% ->
  -0.12% / -0.55% (FAIL -> within, darker as a shadow must be).
- Unchanged: S6, S7 / inn and S14 kept one exposure in every trace (ratio 1.0000). The m4p3 S6 bundle
  darkening (-1.82 / -3.10%) and bx1 -2.31% are real. lr1-3 have no energy gate (one OFF shot).
- 17 energy-gated lean plans hold `qa_exposure_hold=1` from the first cost/shot phase to `restore`; jobs
  expect `exposure held` / `hold released`. Lint `queue_gates.py --lint-exposure-hold` in queue_gates_test.
  Dry runs of nine batches pass. Not authorized; no live run.
- Open: the S8 OFF-b state change (m4r1 +1.3-1.7% at a fixed exposure); predictions for m5w4 and bx1.

## One cached, monitored offline shader compiler (branch worktree-agent-a6e1ec4eea42cb3bc, offline, done, 2026-11-01)

- New `tools/shader_identity.py` (importable + CLI). It compiles requests exactly as `layer/shaders.cpp`
  does, in parallel at BelowNormal priority (`tools/low_priority.py`). It prints progress every 15 s
  (done/total, cached, rate, ETA, failures) and caches each entry's SPIR-V in the main checkout's
  `build/shader_cache`, which every worktree shares.
- Cache key: the DXC binaries and version, the argument template, the request, the entry, the profile,
  and the git blob ids of the source and everything it can include (transitively, conservatively). An
  include edit recompiles only the requests that reach it. A DXC change recompiles everything. A baseline
  commit is read from git blobs. Requests with equal input keys count as identical without compiling.
  Every cache hit re-hashes its blob. Failed compiles are never cached.
- Ported onto it: `tools/compile_shaders.py` (merge-green) and the 7 `tests/*_shader_test.py` gates
  (same CLI, assertions and result files; `--no-cache` forces a full compile). `tests/shader_baseline.py`
  removed. `shader_variant_test` never compiled (it checks `variants.txt` only), so it is unchanged.
- All gates now use the runtime flags (-O3, SHaRC). Before, scalp, hair-origin and bounce compiled
  without -O3.
- New CTest `shader_identity_test` (30 checks, no DXC): include edit -> exactly its dependents;
  DXC change -> all; a new file at a missing include candidate; a macro include depends on the whole tree;
  warm run compiles nothing; a corrupt blob is a miss; failures are not cached; identity states.
- Timings on this PC (8 jobs, BelowNormal, 1374 requests / 1392 entries). Cold `compile_shaders`:
  1607 s. Warm: 1.6 to 10 s. After a one-line edit to `rt/native_hit_shade.hlsli` (240 dependents),
  `rf_hit_shade_shader_test`: 723 s. It compiled only the 240 current-side requests; the baseline
  side came from the cache. `compile_shaders` after that: 1.6 s. Warm `rf_hit_shade_shader_test`
  against an unchanged baseline: 31 s (26 s of that is the first spirv-val of 96 modules, which is
  now cached). The old gates compiled the whole tree twice, about 2 x 1374 requests, every run.

## Gate audit: every live gate replayed on the logs; energy gate made two-sided (branch worktree-agent-a67751e8c0a92555a, offline, 2026-11-01)
- docs/GATE_AUDIT_20261101.md (pointer in META_AUDIT_20261101): 70 gates, 30 OK / 28 weak / 12 broken before,
  43 / 24 / 3 after this branch. Left broken: GI R3 "0 negative px", M5.1 violations, rf_hit_shade "within 9%".
- `tools/m4_promotion_gate.py`: energy two-sided against `<batch>/predicted.json` (committed before the run,
  else "post hoc"); band t x sigma x sqrt(3/2), sigma from OFF-OFF pairs of every lean run on the save (S8
  0.458%, S6 0.390%); nearest-OFF comparisons; non-finite from the scene dump only; plan drift recovered from
  git history or failed; exit 1 on failure; capacity above 0 fails. The old OFF-pair rule flags 46% of noise.
- `scripts/queue_gates.py`: `"event": true` (one-off lines per window) and plan drift. Jobs: m4p1-4 a written
  trace per shot window; m5w4-6 compose per window; m5w5 sum check >= 99% and ratio within 0.1%; m5w6
  refraction per window; pl1 forbids failed BVH audits.
- New readings: m4p3 S6 (plan eeca1fd) bundle steps -1.05 / -1.82 / -3.10% with `rt_emissive=0` (band 1.73%):
  unexplained darkening. m5w4 +2.20% / +2.75% for a light-removing key (band 1.17%). gf1 +3.97%, gf2 +2.87%
  vs predicted -2%: fail. m4p2 capacity: alpha made opaque 0.03 per frame. m5w5 `clamped 46` in one window.
- Tests: m4_promotion_gate_test (added to CTest), queue_gates_test. Dry runs m4_promotion, m5_water, pool_lag
  passed. Before queuing m4_promotion: fill `tools/live_batches/m4_promotion/predicted.json` and commit.

## M5.4c: the game's sun shadow on the water families that sample it (branch m5-water-game-shadow from worktree-agent-a4f18c6ce7ff7c559, offline, READY_FOR_LIVE, 2026-11-01)

- Merged worktree-agent-a09041612a548766f (0994eff, water_audit pre-audit; CMakeLists conflict resolved: both test lists).
- Finding: PL_noCaustics_Shadows 06e66a6f (S3 camp pond) and huge_PL_Shadow ced49ee3 multiply their sun radiance by
  the game's visibility Vg = phi(1, max(0, pow(PCF of 7 Dref samples, 2.2))) (_1365 / _1869), then by two cloud
  layers and b0: every step from Vg to the colour is a product, so the sun term is D = Vg D_u exactly. Replacing Vg
  (not skipping) is what the FS allows.
- `tools/water_audit.py` `game_sun_shadow`: derives Vg and refuses unless every Dref sample reaches C only through
  Vg and Vg only through Lsun's two sun products. New FsIds field `vg`; both rows added (11 id tables).
- Patch (`common/water_fs_patch.h`, `gsh`): two words after the form's last, half2(Dg.rg), half2(Dg.b, Vg) with
  Dg = D (1 - Vg) / Vg = D_u - D (FS without Vg: Dg 0, Vg 1). Without the words a Vg FS stores lum(D) = 0: the sun
  step skips it (game shadow kept, never twice). gsh = false and vg = 0: modules unchanged (FNV pins pass).
- Compose (`WATER_GSH` variants of rt/water_compose.hlsl, 3 new requests): lit ray -> C + Dg, occluded -> R (as
  M5.4), unknown -> C; C' = V (C + Dg) + (1 - V) R. Stats words 44..53, log `[water] game shadow`, rt_debug 26 cyan.
- Key `rt_water_game_shadow=0` (restart to enable, needs rt_water_shadows), GUI "Water game shadow replaced",
  ComposeBuffer.flags (kBufGsh, from pad: no ABI change), stride = form + 8.
- Tests: water_compose_test 4k (FS model: V = Vg gives C within the halves' and target's rounding, Vg = 1 bit-identical,
  V = 0 gives R; 20k random samples x 4 encodings), the gsh patches of all 11 FS; water_audit_test spirv-vals the
  three gsh forms of every FS and checks the Vg refusals. Build + water tests passed (11-01).
- m5_promotion: third job `m5p3-camp-fg2` (Campsite - 27h, gsh at startup, follows rt_water_shadows), plan
  `m5p3_camp.json`; the gate adds the restored light to the prediction. Camp water is small in the 10-30 captures
  (~1% of the frame under the log bridge, 20261030-163004/-163010).
- CTest 82/84 in the build run; the 2 failures were the stale generated files, regenerated (`variants.txt` +3,
  `settings_schema.json`) and both tests rerun: pass.
- Finished on m5-water-game-shadow (merged main with the cascade water variants): the 3 GSH requests also exist with
  RT_CASCADE_MASKS=1;RT_CASCADE_SUN_HIT=1 (6 new); all compile; shader_identity --baseline main --filter water:
  11/11 existing requests byte-identical. m5p3 gets rt_cascade_casters=1, the scene-wait gate and the cascade expect
  lines. m5_promotion dryrun with 3 jobs passes (16.8 min, ~2.46 GB estimate: above ~2 GB, delete traces per job).
  88/88 CTests. Not done: Debris (probe mask _774).

## Hit-material stale scene: per-frame GPU allocations off the commit path (branch worktree-agent-a498dc85e3606abe9, READY_FOR_LIVE, 2026-11-01)

- Symptom (user, in game): with native hit materials on, RT shadows detach while turning and flicker. `[rt] scene
  wait` per 600 frames: 0 timeouts without hit materials (lr1 camp, mean ~5 ms); with them 15-278 (m4i), 3-232
  (m4p4 S8), 5-456 with mean 9.8-12.5 ms in the user's run (the 12 ms cap: frame N traced frame N-1's scene).
- Cause (code read, no timer existed): the provider allocated and freed hundreds of MB of GPU buffers every frame
  (deferred packet 1-4 x 32 MB chunks + rows, query up to 256 MB, UV/basis jobs), freeing in `collect()` from the
  prepass/`scene_prepare` path (under the scene lock) and from `deferred_begin`, which also ran `collect()` on every
  G-buffer draw under the mutex the prepass hooks need per draw; the lighting thread's query held the scene lock for
  its whole recording.
- Fix `rt_hit_material_recycle=0/1` (live, default 0, layer): retired buffers reused (common/native_hit_recycle.h,
  rounded sizes, cap 512 MB, idle 120 frames), `collect()` once per packet/query, query recorded without the scene
  lock. Frame-pairing checks unchanged (rows of another frame still rejected). GPU work and commit order unchanged.
- Measure: `cpu_timing=1` now logs `[cpu] hit material ...` (per phase ms, allocations/frees per frame, MB/frame,
  reused, retained) and "capture end to commit, of which scene lock wait, hit material scene_prepare" on the scene
  build line. `rt_scene_wait_log_frames` (default 600; lean jobs 120).
- Gate: `queue_gates.py --lint-scene-wait` (CTest queue_gates_test, lean_dryrun check): every lean job gates
  `[rt] scene wait` with 0 timeouts in every traced cost window. 27 existing jobs migrated (gate + cadence 120).
  Note: hit-material jobs (m4p*, m4i) now FAIL that gate until their set has `rt_hit_material_recycle=1`.
- Live A/B prepared, not authorized: `tools/live_batches/scene_wait` (S8 + camp, 176 s each, recycle 0/1/0 in
  static and pan windows, exposure held, dryrun passes). Expected with 1: 0 timeouts, wait mean 2-5 ms.
- Tests: native_hit_recycle_test (new), queue_gates_test (+13 checks); `build.ps1 -Test` passes.
- User check after the batch passes: FG on, S8 or camp with the hit-material bundle, rotate with Q/E: shadows stay
  on their casters.

## M5 water promotion package: batch m5_promotion, gate script, Ultra preset commit (branch worktree-agent-a14b8f58ffc130189, offline, READY_FOR_LIVE, 2026-11-01)

- Gate table of today's S8 runs (m5w2-m5w6, each key alone): PLAN_M5_WATER.md "Promotion". Participation passes in
  every window for all six keys; sun shadows with the R = C - D fix: clamped 0 in every window (was 145573), occluded
  62.7%. GPU: build 0.28, reflections 0.12, compose 0.10 (reflections) to 0.24 ms (refraction variant).
- Energy observable corrected. The m4 gate's 5c3fdabc is the lighting before the forward water pass: m5w4 read
  "+2.20% brighter" only because the darker water raised the exposure 2.74%. The post-water image (RR colour input,
  over the probe's b0[0]) gives -9.10% against the compose's own prediction -9.39%: matches.
- Glint noise: one frame per state cannot judge the small steps. Sun glints (up to ~1000) carry about 100% of every
  frame difference; compose and ls single frames read -2.2% / -2.6% against ~0 predicted (open, inside the pooled
  OFF noise of up to 1.7%). The batch takes 3 OFF-a + 3 OFF-b + 3 ON frames with `qa_exposure_hold=1`.
- `tools/m5_promotion_gate.py` (+ `tests/m5_promotion_gate_test.py`, 12 checks; both promotion gate tests now in
  CTest): energy two-sided against the in-run prediction (2 SE of the OFF frames + 5c3fdabc control + window spread),
  non-finite pixels, participation per key over every window, motion rows, water GPU lines.
- Batch `tools/live_batches/m5_promotion` (not authorized): `m5p1-s8-fg2` (Water Queens House) and `m5p2-flymm-fg2`
  (Flymm Cargos Basement - 101h 05m: open water in the save thumbnail; S1 docks loads the foundry now). Chain on at
  launch, live OFF / 4 cumulative steps / ON / OFF, cost windows diag-off, one rt_stats window, 3 debug views, pans
  Q/E with the chain on then off. Dry run passes: 11.2 min, 36 traces, about 1.6 GB.
- Preset commit (separate, merge only if the batch passes): the six water knobs leave Experimental, on in Ultra only;
  the restart ones require sharc_device=1. Settings test checks presets, needs and restart rules.
- Decision rule: both runs valid, every energy row matches with no new non-finite pixel, participation and motion
  rows pass. A Flymm run with an unmapped water family is INVALID: map that family before promoting.
- User check after a merge: Water Queens House on Ultra, the pool next to the columns: the column and rim shadows on
  the water, the tiles under the clear pool shifted toward the camera, reflections of the stone on the water.

## S6 m4p3 darkening: the animated lava and the shot timing, not gi_hit_direct (branch worktree-agent-a128d3e3a3182b5e8, offline, READY_FOR_LIVE, 2026-11-01)

- Shader invariant, offline on the S8 replay pack, same rays: base pass vs hit variant give giRaw.a bit-equal on
  100% of 2.73 M px, mean luminance +0.0755%, 6 px lower beyond the half rounding (depth-test classification
  flips between the two compiled shaders, both ways; lost 3.0e-8 vs added 6.8e-6 per px). New tool
  `py -3 -m tools.gi_hit_invariant --pack <dir>` (gate: alpha bit-equal, mean hit >= base), CTest
  `gi_hit_invariant_test`. The material set clones the scene's alpha bindings, so the occlusion is the key-off one.
- The steps cannot carry their share: diag-on sun added 0.75 vs 11208 for the local lights; instanced rows 0 at
  S6. +inst also contains rf_hit_shade (its tier (a) is brighter, 1.09).
- Localized (`tools/preview_energy_split.py`, decoded PNG previews, within 0.07% of the float means): lava is
  54.5% of the frame's luminance. inst -3.10% = lava -2.56% + rest -0.49%; the rest is -1.07% relative vs
  -0.87% / +0.87% for the two OFF shots. The OFF pair's 0.07% was a cancellation (lava +0.32%, rest -0.40%).
  The lava follows the game's glow buffer 48eedb24: OFF +8% / -8%, steps -5 / -16 / -25%.
- Timing: each m4p3 step shot was traced 1-2 frames after a 4 s DXC compile of its variant (warmup compiled
  only the all-on one). Noted in the m4_promotion README; the plans are unchanged.
- No fix in code (nothing to fix found). Live check prepared, not run: `tools/live_batches/m4_promotion/s6_diag`
  (`m4p5-s6-gidiag`, 4.5 min: S6 replay pack for the invariant, 5 OFF / 4 ON interleaved shots precompiled,
  gi_debug=3 and 2 pairs; predictions in its README). Needs approval of replay_pack_cap_mb=3300 (pack ~3 GB).

## M5 water pre-audit of every dumped FLUID pair; Flymm's family mapped (branch worktree-agent-a09041612a548766f, offline, READY_FOR_LIVE, 2026-11-01)

- Merged `m5-promo-tools` (a88947e). New `tools/water_audit.py`: every Fluids forward pair of the asset DB with
  its dumps; VS contract (gl_Position = vec4(P, 1) * set 0 b2 _m2, y negated, height-only displacement) on the
  spirv-cross GLSL, and the FsIds derived from the raw-id disassembly by the compose model's end of main (mix with
  Refr / a, fog, Lw = direct * mix(1, 1, m) + (Kd + Ks) k, probe and DFG terms, sun Fresnel, three light loops,
  channel, z). It reproduces the three hand-made tables id for id.
- Supported pairs 9 -> 16 (`common/water_policy.h`): + huge_PL_Shadow, flowmap_Brine, huge_PL_SphereMask,
  PL_noCaustics instanced and static, flowmap_OnlyNormals, Debris. Compose id tables 3 -> 9
  (`common/water_fs_patch.h`): + flowmap_PixelLighting, huge_PL_V2, flowmap_Brine, huge_PL_SphereMask,
  PL_noCaustics (both). All 9 patched dumps pass spirv-val in the four record forms (M5.3, sun, local, refraction).
- Not mapped (supported, keep the probe): 06e66a6f PL_noCaustics_Shadows and ced49ee3 huge_PL_Shadow (the sun term
  carries the game's cascade shadow, 7 Dref samples: the M5.4 sun step would shadow twice; needs a sun-step exclusion
  first), Debris (a mask scales the probe term), 1d4bff2e, 857ccb90, 68068cab, 4af3450e (no light loops). Rejected:
  WIP (VS displaces x, y, z). No dump: GlowLichen, flowmap_Shadows, flowmap_Test, Minimap, Reflections instanced
  pairs and 20 static variants.
- Flymm Cargos Basement: level CTY_DockWarehouseBasement_A (planes NAT_RVR_DockWarehouseBasement_Dock_NoSub,
  _Grates) = FLUID_Water_Plane_flowmap_PixelLighting_noCaustics, now mapped in both variants. The m5p2 run is
  expected valid; its `[water] families` line confirms.
- Batch m5_promotion: both jobs `dump_shaders=1` (only modules without a file are written) so one run dumps any
  missing water pair; dry run passes. After it: `py -3 -m tools.water_audit --dump <test tree>\captures\shaders --emit`.
- Tests: water_policy_test (families 9..15, WIP / BloodRiver unknown, unique pairs), water_compose_test (every table
  FS patched; the FNV pins stay on the three earlier FS), new CTest water_audit_test (tables = derivation, refusals,
  spirv-val of every patched module). Keys off: no shader file changed, so the ordinary SPIR-V is byte-identical.
  Cost: none until the keys are on (the same per-pixel stores as the three mapped FS).

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

## FG inputs when the swapchain is larger than the game's output (branch fg-swapchain-extent, READY_FOR_LIVE, 2026-11-01)

- Bug: borderless / fake fullscreen on a 3840x2160 desktop with the game at 3840x1600 never got FG. The UI composite
  draws into the 3840x2160 swapchain and its hudless/UI images are 3840x1600; sl_note_draw matched them by the pass
  extent, so no input was found (gf1 141602: no "[sl] hudless" line, "FG decision ... missing 0xf", displayed 1.00).
- Fix `fg_letterbox=0/1` (live key, layer, default 0): the game's output extent is the pass extent, else the largest
  sampled image inside the pass covering more than a quarter of it (common/fg_extent.h); hudless/UI matched at it;
  the backbuffer tag gets the subrect where the output lands (the composite viewport, else centred) and logs
  "[sl] game output WxH in a WxH swapchain pass". Equal sizes: unchanged. The PQ UI record stays equal-size only.
- Test: fg_extent_test (extent choice, letterbox/pillarbox/scaled, viewport/centred subrect). build -Test passes.
- Live check needed: a 3840x2160 desktop run with fg_letterbox=1 (FG decision want 2, displayed/rendered 2.00, no UI
  ghosting at the letterbox bars). Then default 1.

## rtStats renumber: rf_hit_gi + rt_hit_material_wind_rgb in one tree (branch counter-renumber, offline verified, 2026-11-01)

- Both branches used rtStats 419..426. Merged from main: worktree-agent-a05080ba75170ea35 (rf_hit_gi, keeps
  419..426) then worktree-agent-a87dc09be59810f7c (wind RGB 2S clip check, the later commit: now 427..434).
  kStatsCounters 435 (<= 448, frame_replay's HM_BLOCKERS slots; <= 512). Updated: core/gi.cpp kHitClip3First +
  static_assert, native_hit_material.hlsli base 427, the counter map comment, frame_replay's comment,
  TRACK_D_REFLECTIONS, the wind RGB progress note. No log text or gate regex names the indices.
- Conflicts: main's shader tooling rewrite vs rf_hit_gi's test edits. tools/shader_identity.feature_gate now takes
  a feature predicate and `foreign` (another gate's new requests, here RF_HIT_GI=1); rf_hit_shade's gate owns the
  RF_HIT_GI requests (contract --gi, --em by file); compile_shaders passes --gi. m4p3/m4p4 jobs: rf_hit_gi's plan
  plus main's scene-wait gate (windows recomputed, incl. its new cost windows); m4w job got the scene-wait gate.
- build -Test: 89/89 CTests pass. Byte identity vs main (tools/shader_identity.py --baseline main): 1038/1374
  identical; all 336 others are RT_HIT_INSTANCED=1 requests (wind RGB's family-3 loop in native_hit_material.hlsli,
  compiled in with the instanced key on, whatever rt_hit_material_wind_rgb is). No other request differs, so
  rf_hit_gi off is byte-identical. Still needed: wind RGB's own identity gate decision for the instanced variants.

## Capacity gates in every lean job (branch capacity-gates, offline, 2026-11-01)
- 30 jobs in 12 batches gained the per-window capacity gates of `queue_gates.CAPACITY_GATES` over their traced cost
  windows: `[geo] RT capture` opaque draws dropped 0.00 (all), `[cascade] casters` buffer full 0.0 (windows with
  rt_cascade_casters=1: 17 jobs), plus the patterns that keep those lines. All set rt_timing_frames=120.
- `--lint-capacity` is now a check of every lean dryrun (tools/live_batches/lean_dryrun.ps1, after the scene-wait
  lint) and of CTest queue_gates_test (the whole tools/live_batches tree). Lints ok (capacity, scene wait, exposure
  hold); the 12 affected dryruns pass.

## S6 m4p3 non-finite alarm: the game's depth, not the bundle (branch worktree-agent-ad96013fbc66f09f7, offline, 2026-11-01)

- The `nonfinite 8692` of shot-step-gi was the gate summing every dump of three traces: OFF-a 2917 +
  step 2865 + OFF-b 2910, all in the game's own D32F 2048x2048 depth of pass `76433aef` (also with every
  RT key off). The measured scene colour `5c3fdabc` has 0 non-finite values in all 8 S6 traces.
- `tools/m4_promotion_gate.py`: scene-colour count per trace, other dumps listed per trace and marked
  game-owned or ours. Test case in `tests/m4_promotion_gate_test.py`, now in CTest.
- The darkening (step-gi -1.05%, sun -1.82%, inst -3.10%) stays open: not NaN in the measured buffer.
- Guard in the hit-GI variants only (key-off SPIR-V byte-identical): non-finite or negative hit term
  -> fallback0 (counter 417); mean bounce above the RGBA16F max -> 65504 (counter 418), so no +Inf can
  reach gi_composite's revert (which would drop ~36 px of bounce and keep the occlusion: darker).
  Logged on the `GI hit direct` line. Kept, off with the keys; cost: two predicated counters.
- Rerun m4p3: read `non-finite hit terms (fallback0)` and `bounce over the half-float max` in diag-on.
  Both 0: the darkening has another cause (next: the hit-GI alpha set vs b.alpha, scene drift).

## M5.4 water sun shadows: record R = C - D, not D (branch worktree-agent-abd9303a6e22b012e, READY_FOR_LIVE, 2026-11-01)
- Live m5w4 (S8): shadow geometry right, shadowed pool dark with black and bright specks. `clamped 145573`
  is the 120-frame total (1213 per frame = 0.44% of sun records), not half of the records.
- Audit of the three dumps: D is exactly the FS's sun term times dC/dSun (a, T, X in G; no later multiply,
  no exposure or fog mismatch, no ambient in it, disjoint from M5.3's B). Cause: precision. C - D was
  formed from the B10G11R11 target (1 ulp = 2^-6 / 2^-5 of C) and a half D; where the sun is most of C
  (shadowed pool median 66%, the glint up to C ~ 976) that rounding exceeds the rest.
- Fix (key-on only): the FS stores R = C - D (f32, then half) and lum(D) in words 8-9; the compose writes
  V C + (1 - V) R. Key off byte-identical (M5.3 patch FNV pinned, plain compose SPIR-V SHA1 unchanged).
- Test 4j: the FS model through the real encodings; at V = 0 the compose equals the FS with its sun colour
  set to 0 within half rounding; the old formula fails it.
- m5w4 job: expect_every `clamped` < 120 per window in precheck-shadows-on and cost-on (fails on the old run).
- Expected live: occluded ~62.6%, removed ~80% of D and ~46% of mean C (unchanged: the FS's own sun share),
  clamped ~0, no specks. The shadowed half stays darker than the lit half (its water is mostly sun).

## Box lights `ls_box=2` (branch worktree-agent-ab66efcab091c48ec, offline, READY_FOR_LIVE, 2026-11-01)

- Causes (lr3 S6 08:49:39): 0.009 traced box lights/px because `ls_box=1` traces only the shadow-mapped
  ones (evaluated 1.141/px, listed 2.44/px: not culling); those ran past their shadow view's near plane
  (847,547 of 847,557 occluded). `projector validation px 0`: it counts only under `ls_debug=4`.
  `ls_debug=4` "a quarter": AO 0 x mix(1, AO, m0.y) leaves half the direct light at S7 (m0.y 0.5) in both;
  the residual is additive (the game's in-scatter, not zeroed). Details: TRACK_D_LOCAL_LIGHTS 9.10.
- `ls_box=2` (GUI "All box lights", off): unshadowed boxes traced to the end of their light-side fade-in
  (visibility only, can only darken); shadow-mapped ones only to the near plane. New in-play accuracy
  check `ls box bound` (recompute vs the stored B10G11R11 value's upper neighbour) and `ls box trace`,
  device-local counters reduced per group, copied into rtStats 385-416 (after gi_far 372-382 and the
  rf box part 383-384). `box_light_test` checks the math against f469db78; `bl_reaches` is shared by
  D-LS and the rf_hit_shade box early-out.
- Key-off SPIR-V: before the merge, 992 of 1244 byte-identical vs 904c41e; the rest are the LS_BOX
  variants and the rf_hit_shade ones (same box math, shared). Expected +0.15 to +0.3 ms on the D-LS
  pass. Live: `tools/live_batches/box_lights` (S6 + S7, ~10 min, 0.65 GB; dry run passes). Not run.
- User check: S6 forge and S7 inn with "All box lights": no light leaking into the next room or
  through floors; the main-menu skull keeps its beam.

## D-EM projected check: device-local counters, sliced, timed (branch worktree-agent-a088270f73e9c47a5, offline, READY_FOR_LIVE, 2026-11-01)

- Finding (inferred from m4p3-s6-fg2 112657): `rt_emissive` 0 -> 1 raised the 300-frame average `gi: GPU cost`
  1.65 -> 10.4 ms with flat pass medians: one frame in 16 about 140 ms more. That frame ran the calibration's
  projected check (em_debug.hlsl mode 0, every `rt_em_check_frames=16`): up to 3 emissive ray queries per pixel
  plus 4-14 atomics per pixel into the host-visible `emStats` (PCIe). A periodic hitch for anyone with rt_emissive=1.
- Not a pure diagnostic: `rt_em_calib` scales the approximate families from it, and `rt_hit_emission` skips an
  approximate family until it is calibrated. So it stays on by default, made cheap instead of turned off.
- Change (core + em_debug.hlsl, no ABI, no layer change): counters in a core device-local buffer, reduced per
  workgroup and per wave/record, then a `flush` entry adds the non-zero words into `emStats` (plain stores; the
  host's layout and reads are unchanged). New key `rt_em_check_slices=16`: one interleaved 4x4 slice per frame,
  every pixel still once per 16 frames; 1 = the old full-screen run. The calibration gates count full checks
  (`em::calib_checks`, `calib_px_per_check`, unit-tested); `scripts/qa/em_gate.py` scales N the same way.
- New line per rt_timing_frames: `[em] check GPU cost (N frames): R runs (V views), X ms per run median (check
  pass, max, timed), Y ms/frame; P pixels per run; schedule ...`. The cost is still inside `gi: GPU cost`.
- Expected: per run well under 1 ms (1/16 of the rays, no PCIe atomics), no spike. Not measured yet.
- host_atomics_lint: the D-EM stats entry left the host-visible baseline (14 -> 13 known); `emDev` is device.
- Shaders: only em_debug.hlsl changed (rt_emissive=1 only); every other shader is untouched, so byte-identical.
- Live check (em_forge `ef1-af-em-fg2`, +36 s): phases check-off / check-full (slices 1) / check-sliced (16),
  12 s each; dry run passed. Gate: check-sliced max per run < 1 ms; `gi: GPU cost` mean in check-sliced within
  the check-off spread plus the logged ms/frame; approx families still show `scale` != 1.000.
- Note for readers of the layer's `[em] projected check` / `ours/game per family`: "frames" and bright px per
  frame are now per run (1/16 of the pixels). Docs: TRACK_D_EMISSIVE.md 13.24.

## gi_far go / no-go on the full S8 pack: keep off (branch worktree-agent-a01aebd66c10db204, offline, 2026-11-01)

- Pack `replay-20261101-142819-1` (rc1, casters on, VT cache, lighting inputs 19 of 19). The caster instance has no
  geometry in the pack (tris 0, mask 0), so roofs still come from the asset DB: `PT_ROOFS=1` for the PT and the new
  `frame_replay --sun-trust` (`tools/pt_roofs.py --trust`, 159 padded volumes) for the gi_hit_sun pass.
- PT reference (roofs, real materials, env, sun LUT): far field 53.6% on screen beyond 1 m (visible 15.2, hidden 38.3),
  off screen 35.8% (not split), live domain 8.4%; sun / local 70 / 30%; k = 15.2. Native vs PT far + env:
  E x AO / ref 1.44 (tiles p10 0.90, median 1.75, p90 3.01); the preliminary 0.70 without roofs flips.
- Emulation of gi_far 6 m on the pack matches the live `GI far` line within 2% (visibility 0.833 vs 0.834, bounce
  0.0146 vs 0.0147). Predicted image change: 4 / 6 / 8 m -1.4 / -2.0 / -2.4% of the screen radiance (6 m: ambient
  -2.83%, bounce +0.88%), toward the reference (per-tile error 1.40 -> 1.31 -> 1.30, no overshoot).
- Live S8 measured +2.87% (diffuse +2.93%): even the bounce alone gives only +0.88%, so the live change is not
  explained by the GI far line. Energy gate fails. Kept off; no rule change indicated.
- Next: one live ON / OFF window with `ls_stats=1` and the GI images in the trace (giRaw, folded AO, gDiffuse before /
  after the composite) to find the stage downstream of giRaw; suspect the D-LS `ao_direct` give-back on the folded AO.
- Tools: `frame_replay` prints `GI hit sun` / `GI far`, accepts `--sun-trust`, and `--native-ambient` writes
  per-pixel PFMs (E, as lit, AO, PT total / live / env, k terms). CTests frame_replay_test and pt_reference_test pass.

## User decisions (branch main, decided, 2026-11-01)

- `rf_hit_shade` bias (S8 -7.5%, S6 +9%): option 2. It stays experimental until its bias is within the
  game's own spread. First the GI bounce at the hit (shared with gi_far) and the box-light fix (`ls_box=2`),
  then re-measure; the threshold is not lowered. The user can try it now: Experimental, Native hit
  materials (restart), then Shaded hidden-surface reflections, at S8 Water Queens House.
- `ls_k` stays off in every preset.
- Disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.

## Consolidation report, 2026-11-01

- Done: water M5.1-M5.5 step 1 built and live at S8; hit-material instanced pairing fix; M6 PT reference and replay
  pack with lighting inputs; `gi_far` (off); tooling (every-window gates, atomics lint, progress fragments).
- Merged to main (all off by default): M5.1-M5.5 step 1, M4.4 emission, instanced rows + pairing fix, replay
  writer and inputs, PT reference, `gi_far`, `ls_k` (LS_K variant), queue `expect_every`, atomics lint.
- Not merged: M4 Ultra bundle (promotion batch failed), M5.4 R = C - D fix (pending live), `ls_box=2` (offline),
  `low-prio` (live game keeps Normal priority).
- Open: S6 Forge NaN with the bundle (2.9k -> 8.7k px, being fixed); bundle cost; `gi_hit_sun` at S3; M4.1 motion.
- Try in game: S8 Water Queens House, water options in order (below); S14 instanced foliage; S6 reflections.
- Nothing new is on in Ultra today. Only `rt_cascade_casters` (10-31) is.

## M4 hit shading (Experimental, NOT in Ultra; promotion blocked)

Plan and gate table: [PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md) "Promotion".
- `rt_hit_material` (restart): qualified share of hit lookups S8 48.1%, S6 27.9% (S8 was 0.4%). Classifier
  built share 54.4%. Next families: [MATERIAL_FAMILIES.md](MATERIAL_FAMILIES.md).
- `rt_hit_material_instanced` (startup patch, then live): the wind tolerance theory was disproved live (m4i
  09:39). The real cause was pairing across frames and instance order; fix 4925937 merged. Live at S14: 0 clip
  rejects in every window (gate < 0.04%), qualified 22% -> 37-42%. Cost gi trace +0.14 ms. Gate passes.
- `gi_hit_direct` + `gi_hit_sun` (live): never darker (R3 0 negative px), S8 gi trace +0.52 ms together.
  `gi_hit_sun` S3 gate not run; M4.1 motion and isolated cost not run.
- `rf_hit_shade` (live): bias above (user question). Tier (b) shaded share S8 24%, S6 3%. Cost +1.4 to +1.8 ms,
  unexplained (budget 0.2-0.4 ms); split `hit_ao_rays` 0/2 next.
- `rt_hit_emission` (live, needs `rt_emissive=1` at startup): S6 participation 6.9% of rf lookups.
- **Promotion batch `m4_promotion`: FAILED.** S6 Forge NaN pixels 2.9k (OFF) -> 8.7k (bundle ON). Whole-GI
  cost with the bundle: camp +4.8 ms, S8 +3.4 ms, S6 +9.1 ms. The preset branch
  (worktree-agent-a9e0aabd4d7f18008) stays unmerged. The NaN fix is in progress; rerun after it.

## M5 water (all Experimental, off; live at S8 Water Queens House)

Plan: [PLAN_M5_WATER.md](PLAN_M5_WATER.md). `QuickSave_3784` loads the foundry, so all water jobs run at S8.
- **M5.1** `rt_water_geometry` (live): both S8 planes captured (7b44a567, c4e92ccd), views 120 of 120. No image change.
- **M5.2** `rt_water_reflections` (live): GPU 15 ms -> 0.113 ms (device-local counters). Water in front 7.6%,
  rays screen hit 5.1%, hidden 4.3%. Measured only, no image change.
- **M5.3** `rt_water_compose` (restart): every frame composes now (112-120 of 120, was 3). Compose cost
  7.2 -> 0.098 ms (statistics device-local). The probe stays on 87% of owned pixels; mean change -0.35 to
  -0.65%. Open: traced / probe ratio 0.31 needs a reference crop; one moving window.
- **M5.4** `rt_water_shadows` (restart): live m5w4 shows the right shadow (the far rim shades the far half of the
  pool), but with black and bright specks: 1213 per frame (0.44% of sun records), rounding of C - D in the
  B10G11R11 target. Fix (the FS stores R = C - D) on branch worktree-agent-abd9303a6e22b012e, 80 CTests,
  pending live. Gate: clamped < 120 per ON window.
- **M5.4b** `rt_water_ls_shadows` (restart): live S8 sum check 100% within 0.098%, ratio of the means 1.0000.
  Only 0.2% of S8 water has a local light. Cost 0.18 ms. Passes; little visible at S8.
- **M5.5 step 1** `rt_water_refraction` (restart): live S8 69.6% of transmissive pixels composed, bed shift 24 px
  median, energy +0.07% (within noise), cost 0.24 ms. Passes.
- Next: S1 docks need a save with open sea; `rt_water_hit_shade` for the 8.8% hidden rays.

## M6 PT reference and far field (offline; `gi_far` Experimental, off)

- PT reference (`frame_replay --pt-reference`, merged): no real-time PT now (ReSTIR PT estimate 6.5-9 ms at 4K
  DLSS Performance). The first S8 numbers (sun 99%, 56-86% beyond 1 m) had no roofs; with `PT_ROOFS=1` the far
  field on screen fell 56% -> 33% and the PT total 14x.
- Replay pack with all textures: 3.06 GB. Offline hit material on it: 48.7% qualified, 99.58% bit-equal.
- `--native-ambient`: native ambient vs PT far field + env 0.70 (preliminary: no roofs in that pack).
- `gi_far` / `gi_far_m` (GUI `exp_gi_far` 4 / 6 / 8 m, needs `gi_hit_sun`): merged off. S8 replay bounce 32-54%
  of the PT's visible far tier; +0.8 / +1.2 / +1.6 ms at 4K Performance. Go/no-go needs > ~30% far field
  from a casters-on, VT-cache, native-ambient pack. Live batch `gi_far` prepared, not queued.

## Local lights (D-LS)

- `ls_k` (LS_K variant, GUI `exp_ls_exact_k`): S7 cost 0.717 -> 0.555 ms (k1, -23%). The 3-4% darker buffer was
  the game's bloom min filter, not the estimator (c1951a4). **Stays off: user decision.**
- `ls_box` (Experimental): game box lights are 37.9% of S6 lit luminance, 0.009 traced per pixel, all occluded;
  validation has 0 samples. Branch worktree-agent-ab66efcab091c48ec: `ls_box=2` shadows the 3,329 unshadowed box
  lights, with an accuracy bound and `box_light_test`. Offline, not merged.
- `gi_hit_direct` still skips projector (box) lights at GI hits.

## Tooling (merged unless noted)

- Queue `expect_every`: a gate must hold in every ON window. It turned m4i 093913 and m5w3 083416 INVALID.
- CTest `host_atomics_lint_test`: no new shader atomic into a host-visible buffer (13 baseline targets).
- Progress fragments + `scripts/fold_progress.py`; cb2 retired; coverage HANDLING dated 11-01.
- Offline work at BelowNormal priority (merged). Branch `low-prio` (not merged): the live game keeps Normal
  priority, because BelowNormal starved the level load behind dxc.
- Replay pack (`replay_pack`, `replay_pack_cap_mb`) and `frame_replay` (about 3 s per shader edit).
- `python -m tools.m4_promotion_gate <log>`: the promotion gate table.

## Stage 0 (closed 10-31) and watch items

- Real fps, focused, diagnostics off: camp 60 RT on / 93 off, inn ~71 / 89, forge ~100.
- `rt_stats` + `rt_primary_check` cost ~80 ms per frame: never in cost windows.
- Watch (no cause): camp rug/plank rectangles, Astarion hairline, dialogue shadow flicker.
- Inn rotation: faded walls keep casting shadows (user decision). `pool hit match` 34-37% rotating vs 99.6%
  static is not explained (META_AUDIT_20261101 3).

## How to test in game

All in the settings window, section Experimental (off in every preset). Leave the preset on Ultra, so GI is
Ray-traced, Reflections are on and Roof sun shadows (cascade casters) are on. "Restart" means: turn it on,
quit, start the game again. Turning any of these off is live.

**Water** (S8 Water Queens House, the pool under the temple and columns). In this order, each needs the one before:
1. Water geometry capture (`exp_water_geometry`, live). No visible change.
2. Water reflections, measured (`exp_water_reflections`, live). No visible change; debug views only.
3. Water reflections, composed (`exp_water_compose`, **restart**). Reflections of the columns and temple on the pool.
4. Water sun shadows (`exp_water_shadows`, **restart**). The far half of the pool in shadow. Known bug until the
   R = C - D fix merges: black and bright specks in the shadowed half.
5. Water local-light shadows (`exp_water_ls_shadows`, **restart**). Almost nothing at S8 (0.2% of the water lit).
6. Water refraction (`exp_water_refraction`, **restart**). The pool bed should shift with the view, not slide.

**Hit materials** (reflections and bounce of surfaces the screen does not show):
1. Native hit materials (`exp_hit_material`, **restart**). Everything below needs it.
2. Wind and two-sided foliage (`exp_hit_material_instanced`, **restart** to enable, then live). S14 foliage.
3. Shaded hidden-surface bounce (`exp_gi_hit_direct`, live).
4. Sunlit hidden-surface bounce (`exp_gi_hit_sun`, live, needs 3). S3 Campsite 27h: shaded areas get
   bounce from sunlit ground; it must never get darker.
5. Shaded hidden-surface reflections (`exp_rf_hit_shade`, live). S8 polished stone: surfaces behind the camera
   show lit, not the flat probe colour. About 7% darker than the screen at S8, 9% brighter at S6.
6. Emission at ray hits (`exp_hit_emission`, Hidden hits, live; needs Emissive geometry capture `rt_emissive=1`
   at startup). S6 Adamantine Forge: lava and braziers in reflections.

**Known bug:** S6 Forge with the whole hit bundle on shows NaN pixels (black or white dots, 8.7k vs 2.9k off).
Being fixed. Report the save and the option combination if you see dots elsewhere.

Ctrl+Shift+F9 switches every RT effect off and on. These options cost GPU time (bundle: +3.4 to +9.1 ms whole
GI, water about 0.6 ms with every water option); quality is not qualified for Ultra yet.

## Milestones

| Milestone | State |
|---|---|
| M0 | Queue authorization and ownership done. |
| M1 | Watch items above; offline cutout fixes ready. |
| M2 | 27 native-matched components; room closure unproven. Only when an offscreen need requires it. |
| M3 | Hit materials: S8 48.1%, S6 27.9%, S14 37-42% qualified. |
| M4 | All keys built and live; Ultra promotion blocked by the S6 NaN and cost; `rf_hit_shade` bias is the user's call. |
| M5 | M5.1-M5.5 step 1 live at S8, off; M5.4 speck fix pending live; S1 not measured. |
| M6 | PT reference offline; `gi_far` off, waiting for a roofs-on reference pack. |
