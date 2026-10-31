# Track D: hardware ray tracing (index)

Track D runs ray queries on the game's own Vulkan device. The host layer captures the
G-buffer geometry every frame (transform feedback), builds BLAS/TLAS (soup plus the
static/dynamic split), and the core traces the sun, AO, GI, local-light shadows (D-LS),
reflections (D-RF) and uses the emissive data (D-EM). Off by default behind `rt=0`;
see [QUALITY.md](QUALITY.md) for the rules and [COVERAGE.md](COVERAGE.md) for the gaps.

This file was split on 2026-10-31. The content now lives in:

- [TRACK_D_SCENE_BUILD.md](TRACK_D_SCENE_BUILD.md): capture, BVH, mesh cache, phase 2 results,
  the static/dynamic split, partition spill, `rt_scene_wait`, alpha stream CPU cost.
- [TRACK_D_ALPHA_GI.md](TRACK_D_ALPHA_GI.md): alpha-tested geometry, hybrid RT GI, effect catalogue.
- [TRACK_D_LOCAL_LIGHTS.md](TRACK_D_LOCAL_LIGHTS.md): D-LS local-light shadows, box lights (9.10 and on).
- [TRACK_D_REFLECTIONS.md](TRACK_D_REFLECTIONS.md): D-RF reflections, unknown queries, shared reflection origin.
- [TRACK_D_SUN.md](TRACK_D_SUN.md): sun replace and the current native-caster sun scene.
- [TRACK_D_EMISSIVE.md](TRACK_D_EMISSIVE.md): D-EM emissive geometry capture (13.1 to 13.21 and on).
- [NATIVE_HAIR.md](NATIVE_HAIR.md): the dated native Hair, Scalp and Pubes entries (end of the file).
- [archive/TRACK_D_P1_HISTORY.md](archive/TRACK_D_P1_HISTORY.md): P1 history of 2026-10-29
  (FG-on gate, rotation and epoch diagnostics, rebase, transient flashes).

## Old section number -> new location

Section and subsection numbers are unchanged in the new files ("9.10" is still "9.10").
There were two sections numbered 13: "13 (RF)" below is the first, "13 (D-EM)" the second.

| Old section | New location |
|---|---|
| 1 Survey | [TRACK_D_SCENE_BUILD.md#1](TRACK_D_SCENE_BUILD.md#1-survey-how-the-g-buffer-vertex-shaders-form-positions) |
| 2 Mechanism | [TRACK_D_SCENE_BUILD.md#2](TRACK_D_SCENE_BUILD.md#2-mechanism-transform-feedback-of-gl_position) |
| 3 Results | [TRACK_D_SCENE_BUILD.md#3](TRACK_D_SCENE_BUILD.md#3-results) |
| 4 Validation | [TRACK_D_SCENE_BUILD.md#4](TRACK_D_SCENE_BUILD.md#4-validation) |
| 5 Phase 2 plan | [TRACK_D_SCENE_BUILD.md#5](TRACK_D_SCENE_BUILD.md#5-phase-2-plan-blastlas-and-ray-query-shadowsgi-as-planned-section-6-has-what-was-done) |
| 6 Phase 2 results | [TRACK_D_SCENE_BUILD.md#6](TRACK_D_SCENE_BUILD.md#6-phase-2-results-acceleration-structures-and-ray-queries-on-the-games-device) |
| 7 Alpha test, hybrid GI | [TRACK_D_ALPHA_GI.md#7](TRACK_D_ALPHA_GI.md#7-phase-3-2026-10-26-alpha-tested-geometry-and-hybrid-rt-gi--paused-wip) |
| 8 Effect catalogue | [TRACK_D_ALPHA_GI.md#8](TRACK_D_ALPHA_GI.md#8-effect-catalogue-and-roadmap-analysis-2026-10-26) |
| 9 D-LS (9.1 to 9.x, box lights 9.10+) | [TRACK_D_LOCAL_LIGHTS.md#9](TRACK_D_LOCAL_LIGHTS.md#9-d-ls-ray-traced-local-light-shadows-implemented) |
| 10 D-RF | [TRACK_D_REFLECTIONS.md#10](TRACK_D_REFLECTIONS.md#10-d-rf-ray-traced-reflections-implemented-tiers-a-and-c) |
| 11 BVH split | [TRACK_D_SCENE_BUILD.md#11](TRACK_D_SCENE_BUILD.md#11-staticdynamic-bvh-split-instanced-object-space-blas-rt_bvh_split1) |
| 12 Sun replace | [TRACK_D_SUN.md#12](TRACK_D_SUN.md#12-sun-replace-the-traced-visibility-replaces-the-games-sun-mask-rt_sun_replace-quality-r0) |
| 13 (RF) Unknown queries, shared origin | [TRACK_D_REFLECTIONS.md#13](TRACK_D_REFLECTIONS.md#13-unknown-ray-queries-shared-reflection-origin-qa-local-light-knob-2026-10-27) |
| 13 (D-EM), 13.1 to 13.21 | [TRACK_D_EMISSIVE.md#13](TRACK_D_EMISSIVE.md#13-d-em-data-emissive-geometry-capture-rt_emissive1-2026-10-27-data-side-only) |
| Current native-caster sun scene | [TRACK_D_SUN.md](TRACK_D_SUN.md#current-native-caster-sun-scene-default-off) |
| Split capture partition spill | [TRACK_D_SCENE_BUILD.md](TRACK_D_SCENE_BUILD.md#split-capture-partition-spill-2026-10-31-offline-candidate) |
| P1 rt_scene_wait modes | [TRACK_D_SCENE_BUILD.md](TRACK_D_SCENE_BUILD.md#p1-rt_scene_wait-modes--2026-10-31) |
| Base frame CPU cost (`rt_alpha_stream`) | [TRACK_D_SCENE_BUILD.md](TRACK_D_SCENE_BUILD.md#base-frame-cpu-cost---2026-10-30-default-off-candidate) |
| Native Hair, Scalp, Pubes entries (top) | [NATIVE_HAIR.md](NATIVE_HAIR.md#dated-entries-moved-from-track_d_rtmd-2026-10-30) |
| Live FG-on gate, P1 FG ON, rotation, translation, flashes | [archive/TRACK_D_P1_HISTORY.md](archive/TRACK_D_P1_HISTORY.md) |
