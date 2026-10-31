# Consolidation plan (2026-10-31, main d3e06e1)

Read-only inventory of main after four days of autonomous work (6bd6967..d3e06e1: 1,806 files,
+93,697 lines: docs 43k, tests 20k, layer 9.9k, shaders 7.1k, tools 5k, common 3.8k, core 0.8k).
Nothing below has been changed yet. Each item has an ID so the user can approve or reject it.

## A. Performance on the default path (highest value)

- **A1. `rt_scene_wait=1` (default ON) blocks the recording thread up to 12 ms per frame**
  (`layer/rtscene.cpp:1306-1320`, `:1342`; ini notes a mean wait of 4.6-4.9 ms, max ~10 ms). At
  ~85 fps (11.8 ms per frame) this is a real CPU cap. It is the fix for the rotation detachment
  (match 33% to 97%), so it stays. Action: measure `rt_scene_wait` 0/1 fps in one run (needs a
  live batch); then replace the CPU wait with a GPU wait (timeline semaphore) so the frame is not
  stalled. Also fix the fallback mismatch (code 8, settings.h 12). Effort M.
- **A2. Settings read once per captured draw.** `cfg_int` (`layer/util.cpp:308-336`) builds a
  string, does two hash lookups and takes a global mutex; it runs per draw in `geo_pre_draw`
  (`geocapture.cpp:2192/2222/2233/2264/2286`), per render pass (`dump.cpp:654`) and per wait call
  (`cmd.cpp:991/1001`). Calls in layer/ grew from ~99 to ~213. Estimated 1-3 ms CPU per frame plus
  lock contention. Action: one settings snapshot per frame in `cfg_poll` (struct of atomics). M.
- **A3. Small always-on costs** (each S): terrain-dump hooks lock on every submit and command
  buffer begin (`layer.cpp:494`, `terrain_packet_dump.inl:203-218`): early return on an atomic flag;
  `g_cpu_draws.fetch_add` on every draw even with `cpu_timing=0` (`cmd.cpp:662/672`); OMM content
  tracking stays armed after `rt_omm` is switched off at runtime (`omm_content.cpp:151`).

## B. Correctness and robustness (each S)

- **B1.** `geocapture.cpp:1893` `new EmPass` leaks when a command buffer is reset with the pass open
  (acknowledged at `cmd.cpp:447`): free it in Begin/Reset.
- **B2.** `CmdState::geo` is a `void*` holding three types told apart by flags (`host.h:221`):
  replace with a tagged variant.
- **B3.** Sun-path waits hold `g_cc_mu` through a condition variable on a game thread
  (`geocapture.cpp:2516/2538`): release the lock before waiting.
- **B4.** Dead function `geo_sun_cascade` (`geocapture.cpp:2499`, declared `host.h:539`): delete.

## C. Settings: 84 keys default OFF

None qualifies for promotion yet. Closest: `rt_cascade_casters` (user check positive, +1.7 ms, no
formal gate) and `ls_box` (user check positive, S5/S6/S7 gates open). About 14 are parameters where 0
is the chosen value, and ~45 are diagnostics (fine to stay off; no GPU cost when off).

- **C1. Remove rejected or superseded research keys and their code** (numbers in the docs):
  `rt_scene_rebase` (24 refs, 51 wrapper shaders; check gi_hit_direct does not depend on it),
  `rt_scene_camera_match` + host `rt_scene_camera` (ABI 16), `rt_ao` (never runs with `gi_rt=1`; 56
  refs), `ls_k` (over budget, follow-up failed), `rt_capture_depth_write` (no recorded result),
  `rf_so`, `rt_em_filter`, `rt_fast_trace`, `rr_pre_exposure`, `rt_prepass_by_ordinal`, `rt_sun_top`,
  `rt_cascade_roofs_trust`, `gi_bounce_spec`, `ls_box_unshadowed`, `geo_xfb_always`.
  Each removal: key, schema, GUI, shaders and tests together; the remaining shaders must compile
  byte-identical. Effort M overall.
  **Done 2026-10-31** (branch `consolidation`; also `rt_ao_rays`/`rt_ao_radius`, `ls_rest_samples`,
  `rt_em_aniso`, `rt_sun_top_pad`; ABI 22). Kept: the `RT_SCENE_REBASE` blocks of rt_common.hlsli
  and hair_receiver.hlsli (tools/native_hair_gpu_probe rebase cases), the QA analyzers keyed on
  `rt_scene_rebase=1` plan steps (scripts/qa/rebase_gate.py, fg_motion_gate, rotation_preflight,
  prepare-p1-fg-matrix.py) and historical job fixtures. Push fields stay (layout), always 0.
- **C2. Keep experimental, with a gate to finish:** `rt_cascade_casters`, `ls_box`,
  `rt_native_scalp`, `rt_native_hair`, `rt_hair_physical_origin`, `rt_omm`, `rt_sun_cascade_current`,
  `rt_hit_material`, `gi_hit_direct`, `rt_alpha_spill`, `rt_emissive`, `sharc`, `fg_ui_pq`.
  The first two are the next promotion candidates (one live batch each).
- **C3.** Keys that keep their resources until restart once enabled (`rt_hair_physical_origin`,
  `rt_native_scalp`, `rt_emissive`, `rt_hit_material`; `sharc_device=1` enables device features even
  with `sharc=0`): document in USER_SETTINGS.md; no code change now.

## D. Architecture (larger, after A-C)

- **D1.** Emissive family formulas and calibration live in the host (`layer/emissive.cpp:388/466`):
  move the policy to core so it hot-reloads. L.
- **D2.** Hit-material qualification policy (`geocapture.cpp:2116-2124`, `layer/hit_material.cpp`)
  and the sun native-packet table (`layer/sun_scene.cpp:171`) to core/common tables. M / S.
- **D3.** Diagnostic-only host code (`native_light_validation.h`, `terrain_packet_dump.inl`) behind a
  build option or moved to tools. M.
- **D4.** Shader variants: 1,209 three-line wrapper files from flag combinations. Pass defines to DXC
  instead of adding files. M.
- **D5.** ABI 8 to 21: of 7 additions, 3 are diagnostic or research (`pan_capture` 15,
  `rt_scene_camera` 16, `native_validation` 17). Remove them in one ABI bump together with C1.
  Fix stale "ABI 9" comments in host_api.h.

## E. Branches and worktrees (70 unmerged refs, 33 worktrees)

- **E1. Merge (real work, small):** `codex/user-regression-ab` (Hair receiver guard A/B switch),
  `codex/omm-bake` tip (TileBlend shadow capture; merge main into it first), and the staged
  `codex/progress-trim` in the main checkout (Codex is finishing it now). Through merge-green.
- **E2. Drop refs fully contained in main** (~37: 24 with every patch in main, 11 with every subject
  in main, `mv-jitter-optin`, `tileblend-gpu`). First check the two unique commits
  native-coverage f9d5b86 and native-hit-material a15f5a3. Remote copies stay on GitHub unless
  the user wants them deleted too.
- **E3. Archive (keep refs, nothing to do):** research branches of 10-27..10-29 (camera-native,
  research-preserved, p1-pan, FG/camera WIP, worktree-agent-*, sup/cascade-alphatest).
- **E4. Remove 7 inactive worktrees** (motion-jitter, pan-motion-vectors, en2-selfcheck, p4-omm,
  p6-water, p2-emissive, p2-pending), unlinking the `external` junction first; ~15 more after 24 h of
  inactivity.

## F. Documentation (48 docs, 123 evidence files)

- **F1.** Move ~13 historical docs to `docs/archive/`: AUDIT_ENGINE_DATA, AUDIT_RT, AUDIT_SOTA,
  DEVICE_LOST_DLS, P1_PAN_FLASHES, P2_EMISSIVE_OVERLAP, P2_PENDING_MERGES, P3_NATIVE_DIRECT_VALIDATION,
  ROADMAP, STATUS, SUN_PENUMBRA_REVIEW, UI_CONTAMINATION_OFFLINE, research.md.
- **F2.** Merge duplicates: 4 OMM docs into P4_OMM; HAIR_PHYSICAL_ORIGIN into NATIVE_HAIR;
  P2_EMISSIVE_LEGACY_HANDOFF into TRACK_D; GOAL_RENDERING_AGENT into the plan (or archive).
- **F3.** One status source: HANDOFF_SESSION.md (state, how to resume) + PROGRESS_CODEX.md (recent
  log). Fix stale facts (ABI 19 mentions, plan checkpoint 90d7930, "fix the two regressions first"
  although the user can no longer reproduce them). Add `docs/README.md` as an index.
- **F4.** Split TRACK_D_RT.md (3,785 lines) by effect (sun, local lights, reflections, scene build).
- **F5.** One naming legend: P1-P6 phases vs M0-M6 milestones vs S1-S8 scenes vs R0-R8 runs.

## G. Open user-visible items (status today)

| Item | Status | Next |
|---|---|---|
| Camp rug/plank rectangles | not reproducible since 10-31, not attributed | keep `rt_alpha_spill` A/B batch ready; close if not seen again |
| Astarion hairline band | not reproducible; real Hair/Scalp identity gap found | merge E1 guard switch; include in next trial |
| Dialogue shadow flicker (new, slight) | open, no cause yet | temporal captures in the next live batch |
| Box-light skull beam / red gem flicker | open (art-intent rule for unshadowed box lights) | part of the `ls_box` gate |
| Black floor squares, roof leak, red blocks, pan/rotation, FG UI | fixed in main | user confirmation pending for some |

## Proposed order
1. E1 merges, E4 worktrees, E2 ref cleanup (low risk, quick).
2. A2, A3, B1-B4 (small code fixes, offline, testable with CTests).
3. C1 + D5 in one ABI bump (removals), byte-identical shader check.
4. F1-F5 documentation.
5. One live batch: A1 measurement, `rt_cascade_casters` and `ls_box` promotion gates, dialogue
   flicker captures, E1 hair guard. Then promote what passes.
6. D1-D4 architecture, then back to the plan (M3/M4, then M5 water).
