# BG3RTX progress - 2026-11-01 (main 5202bc2, ABI 24)

## M4.4 rt_hit_emission (branch hit-emission, offline, READY_FOR_LIVE, 2026-11-01)

- `rt_hit_emission=0` (GUI `exp_hit_emission`: Off / Hidden hits / All hits; live; needs `rt_emissive=1`,
  `rt_hit_material=1`, acts through `gi_hit_direct` and/or `rf_hit_shade`): a GI bounce or reflection ray
  hitting a captured emitter adds its D-EM record radiance, with the projected check's own lookup
  (`rt/native_hit_emission.hlsli`). Exact families, calibrated approximate ones; excluded never.
- Finding: the screen value has no emission (prevRadiance is before compose adds the effects target), so
  `1` = hidden hits (GI zero-fallback hits, shaded D-RF tier b), `2` = also GI screen hits and tier (a).
  SHaRC hits untouched (already injected). No ABI change: host emissive set as GI set 2 / D-RF set 5.
- Offline: key off byte-identical (`tests/hit_emission_shader_test.py`), every `*_em` variant compiles and
  matches its contract (`--em`); GPU fixture case 7 (emissive quads) 5/5 PASS; 71 CTests pass.
- Not built: emitters as light sources at hidden hits (no CDF in the light list; double count with the
  game's authored lights near lava/braziers): TRACK_D_EMISSIVE 13.23. Cost not measured. Live gate there.

## M5.1 water geometry (branch m5-water, offline, READY_FOR_LIVE, 2026-11-01)

- `rt_water_geometry=0` (GUI `exp_water_geometry`, live): forward water draws of the 7 audited FLUID_Water_Plane
  FS/VS pairs go through the D-EM pass hook into their own XFB soup, BLAS and TLAS per submission
  (`layer/water.cpp`), with a record per draw (Roughness_Base, Reflectance_Base). Policy `common/water_policy.h`,
  run by the core via em_record (`BG3_EM_REQ_WATER`): no ABI change. Bound nowhere: no image change.
- Offline answers: water VS displace Y only (flowmap + scrolling noise), XFB gets the animated positions; water
  writes no depth (D24S8 identical across all ten traced water passes).
- Key off: no hook, no allocation; no shader changed (741/741 compile). 69 CTests (+water_policy_test), QA pass.
- Live: `tools/live_batches/m5_water/` (5 min, dry run passes). Details: PLAN_M5_WATER "M5.1 status".

## Offline frame replay, steps 1-2 for gi_hit_direct (branch frame-replay, offline, 2026-11-01)

- `binrame_replay.exe` (tools/frame_replay.md): loads a replay pack (common/replay_pack.h), rebuilds BLAS/TLAS
  from InstInfo + soup/pool + the instance array, binds both sets, compiles the REAL `gi_trace_rt_hit_direct.hlsl`
  with the layer's DXC flags and dispatches it; writes giRaw (raw + PFM), the live `GI hit direct` / `lights` /
  `policy` / `hit material` lines and the GPU time. About 3 s per shader edit.
- CTest `frame_replay_test`: synthetic lit + dark packs; 356 hidden hits qualified (reason 0), supported and
  nonzero; two dispatches bit-identical; dark bounce exactly 0 with giRaw.a bit-equal. 69 CTests pass.
- Not built: the live `replay_pack` writer (needs one host call, ABI 25; design in tools/frame_replay.md).
  The live job `tools/live_batches/replay_capture/` (S8, one pack) passes its dry run and waits for it.

## M4.3 gi_hit_sun + shared sun roof rule (branch gi-hit-sun, offline, READY_FOR_LIVE)
One current summary. Older entries, verbatim: [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md)
(10-31 evening to 11-01) and [archive/PROGRESS_CODEX_2026-10-31.md](archive/PROGRESS_CODEX_2026-10-31.md) (before).
Plan: [RENDERING_EXECUTION_PLAN.md](RENDERING_EXECUTION_PLAN.md). Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md).

## Open questions for the user

- None blocking. Promotion of any M4 key to the Ultra preset waits for its gates (below).
- Not blocking (M4.4): the plan said screen hits already contain emission; the code says they do not
  (prevRadiance is before compose). `rt_hit_emission=2` adds it at screen hits too. The live gate tests 1
  and 2; which one to qualify for presets is your call after it (conservative default: 1).

## Stage 0: closed (2026-10-31 evening)

- Consolidation done ([CONSOLIDATION.md](CONSOLIDATION.md)). Live batch cb1-cb7 and lean regression
  lr1-lr3: no regression against pre-consolidation (same pass costs, same primary match).
- Real fps, focused, diagnostics off: camp 60 RT on / 93 off, inn ~71 / 89, forge ~100.
- `rt_stats` + `rt_primary_check` cost ~80 ms per frame: never inside cost windows (lean jobs).
- `rt_cascade_casters` ON in Ultra (roof sun shadows; +0.3 to +1.3 ms; user confirmed outdoor roofs).
- `ls_box` stays Experimental (S6: 99.999% of box rays traced occluded, suspect; no accuracy gate yet).
- `rt_scene_wait=2` removed (stale scene after rotation). Lean batches: ~11 min and 0.4 GB instead of
  90 min and 35 GB.
- Meta-audit of what was closed too early: [META_AUDIT_20261031.md](META_AUDIT_20261031.md).

## Stage 1: M4 offscreen reflections and GI hit lighting (in progress)

Plan and gates: [PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md). Every key below is off by default
and Experimental in the GUI.

### Hit material coverage (`rt_hit_material`, restart)
- Qualified share of hits, live: S8 0.4% -> ~17%, S6 ~6.7%.
- What landed: capture caps lifted (rows 4096, samplers 64, views 256), pool hit path for BVH-cached
  statics (STI 2d2ac3c3/14678345: UV/basis capture, pool rows, pool lookup), reflectance-variant pairs
  (2d2ac3c3/3df678c0, 2d2ac3c3/81657854, 3812bc98/3da860be), reasons logged by the real consumers.
- Ranking of what is left: [MATERIAL_FAMILIES.md](MATERIAL_FAMILIES.md). Built pairs hold 31.9% of the
  combined share. Largest next: 54df894a/613ead9c (12.4%, NEW recipe); cheap: 50fbfceb/8e49cde8 (COPY, 3.9%),
  1e9145e4/923131c7 (VARIANT, 2.4%).

### Reflections tier (b): `rf_hit_shade` (live)
- In-frame check on tier (a) hits, ratio L_b / L_a (hit shading against the game's screen result):
  - S8 Water Queens House: **0.95** with `rt_cascade_casters` on. AO estimate at the hit 0.805 against
    the screen's 0.802.
  - S6 Adamantine Forge: **1.48**. Cause analysed offline ([LIGHT_CONVENTIONS.md](LIGHT_CONVENTIONS.md) 4):
    not units, b13, PreExpose or BRDF; box lights at hits skipped the game's AO-on-direct (x1.32 on box
    diffuse). Fix landed after the run (a9b7e75): unmeasured.
- Hit lighting now matches the screen's light policy: D-LS light set, projector (box) lights at hits,
  local diffuse probes in the ambient, exact hit point, AO estimate, per-term log.

### GI hidden-hit bounce: `gi_hit_direct` (live)
- Nonzero contribution live (was 0 of 22871 supported: the ray traced into the light's own fixture;
  it now stops `ls_clear` short of the light, as D-LS).
- Motion qualification and isolated GPU ms (M4.1) still open.

### Sun at GI hidden hits: `gi_hit_sun` (live, merged)
- Landed with the shared R0 roof rule: without casters, a hit under a roof volume (or with volumes
  unknown) gets no sun, counted. The same rule applies to `rf_hit_shade`. Live gate (S3 OFF/ON/OFF) pending.

### Emissive (D-EM, `rt_emissive` stays 0)
- AF gate: 5 of 6 families pass.
- 304be1c7 excluded (`rt_em_fx_pulse=0`): the record matches its shader term for term; the deficit is
  additive, the game adds a non-pulsing light there. TRACK_D_EMISSIVE 13.22.
- 159a6211 is legitimately 0 (GlowMultiplier 0 in its live record).

### Inn rotation primary match (~83%)
- Pool not lagging: mesh audits while the camera moves, 0 failed ([INN_ROTATION_MATCH.md](INN_ROTATION_MATCH.md)).
- About half of the "in front" mismatch is camera-faded walls (`rt_primary_fade_class`). User decision:
  faded walls keep casting shadows in every RT effect; only the measurement excludes them.
- In normal play the scene is ready in time (scene wait got this frame's scene, 0 timeouts).

### Offline tooling (no game launch)
- `python -m tools.material_families`: ranking of G-buffer shader families with COPY/VARIANT/NEW class.
- `native_hit_gpu_fixture` (CTest): windowless analytic GPU fixture of the hit-shading functions, 23 checks.
- `python -m tools.assets.check_conventions`: light conventions of the asset DB (flags, units, box ramps,
  roofed rooms). Found the box join denominator (fixed 12e592b) and the Kelvin x Color rule.
- Ranked offline work: [OFFLINE_OPPORTUNITIES.md](OFFLINE_OPPORTUNITIES.md) (B, F, D done first; A frame replay next).

### Incident (2026-11-01)
- Removing a worktree followed its NTFS junctions and emptied the shared SDKs (`external/`) once.
  Restored. Worktrees are now removed with a safe script that unlinks the junctions first and checks
  the targets afterwards (AGENTS.md "Never").

## How to test in game

All in the settings window, section Experimental (off in every preset). Leave the preset on Ultra so
GI is Ray-traced, Reflections on and Roof sun shadows (cascade casters) on.

Dependency order:
1. **Native hit materials** (`exp_hit_material`): **restart required**. Everything below needs it.
2. Then, live (the first enable compiles shaders, a short hitch):
   - **Shaded hidden-surface bounce** (`exp_gi_hit_direct`): needs Global illumination Ray-traced.
   - **Sunlit hidden-surface bounce** (`exp_gi_hit_sun`): needs Shaded hidden-surface bounce on.
   - **Shaded hidden-surface reflections** (`exp_rf_hit_shade`): needs Reflections on.

Where to look:
- **S8 Water Queens House** (polished stone): reflections of surfaces behind the camera or behind
  columns should show the lit surface instead of the flat probe colour. Toggle the reflections option
  off and on at the same view.
- **S6 Adamantine Forge**: same check; reflections there may still read too bright (the 1.48 run predates
  the box fix). Report it if they do.
- **S3 Campsite - 27h** (outdoor): GI bounce from sunlit ground and walls into shaded areas with the sun
  option on; it should never get darker when an option is turned on.
- **Inn interiors**: camera-faded walls still cast shadows by design; not a defect.
- **Ctrl+Shift+F9** switches every RT effect off and on for a whole-lighting comparison.

Costs of these options are not measured yet; quality is not qualified. Report visible defects, the save
and the option combination.

## Watch items (not reproducible since 10-31, no cause established)

- Camp rug/plank rectangles: `rt_alpha_spill` ready for an OFF/ON/OFF if they return.
- Astarion hairline band: Hair/Pubes/Scalp share class 5 (real identity gap); guard ready, image A/B open.
- Slight dialogue shadow flicker: source not established; needs temporal native/RT evidence.
- GI R3 (descriptor fix): VALID run, but final image review, motion and visible gain unqualified.

## Milestones

| Milestone | State |
|---|---|
| M0 | Queue authorization and ownership done; respect the live boundary. |
| M1 | Watch items above; offline cutout fixes ready. |
| M2 | 27 native-matched components; room closure unproven. Only when an offscreen need requires it. |
| M3 | Hit materials: S8 ~17%, S6 ~6.7% qualified; more families next. World lights open. |
| M4 | rf_hit_shade, gi_hit_direct, gi_hit_sun built and running live; gates open (above). |
| M5 | Water: M5.1 `rt_water_geometry` in progress offline ([PLAN_M5_WATER.md](PLAN_M5_WATER.md)). |
| M6 | Shared opaque reference / PT research last. |
