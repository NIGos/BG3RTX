# BG3RTX documentation index

One line per current doc. Superseded docs are in [archive/](archive/). Evidence files
(JSON, logs, contact sheets) are in [evidence/](evidence/); prepared live jobs that are
not yet authorized are in [live-test-drafts/](live-test-drafts/).

## Start here

- [HANDOFF_SESSION.md](HANDOFF_SESSION.md): current state, user steering, how to resume. The one status source.
- [PROGRESS_CODEX.md](PROGRESS_CODEX.md): recent progress log and open questions for the user.
- [../AGENTS.md](../AGENTS.md): rules for every agent working in this repo.
- [RENDERING_EXECUTION_PLAN.md](RENDERING_EXECUTION_PLAN.md): the rendering program, milestones M0 to M6 and their gates.
- [USER_SETTINGS.md](USER_SETTINGS.md): user guide for the launcher and settings window.

## How things work

- [ARCHITECTURE.md](ARCHITECTURE.md): host layer, hot-reloaded core, the C ABI between them.
- [FRAME_MAP.md](FRAME_MAP.md): BG3's Vulkan frame, pass by pass.
- [ASSET_DB.md](ASSET_DB.md): the offline game data (lights, materials, roofs, probes) and how it is built.
- [SE_BRIDGE.md](SE_BRIDGE.md): Script Extender bridge for game state the GPU never sees.
- [SETTINGS_DESIGN.md](SETTINGS_DESIGN.md): settings table, presets, layered ini, launcher, cost budget.
- [COVERAGE.md](COVERAGE.md): what the game draws against what our RT handles, gaps ranked by screen impact.

## Per-effect TRACK docs

- [TRACK_A_RR.md](TRACK_A_RR.md): DLSS Ray Reconstruction in place of DLSS SR, RR before post.
- [TRACK_B_FG.md](TRACK_B_FG.md): DLSS Frame Generation through Streamline.
- [TRACK_C_GI.md](TRACK_C_GI.md): screen-space GI.
- [TRACK_D_RT.md](TRACK_D_RT.md): hardware RT index, with the map from old section numbers to the files below.
  - [TRACK_D_SCENE_BUILD.md](TRACK_D_SCENE_BUILD.md): geometry capture, BVH, static/dynamic split, `rt_scene_wait` (sections 1-6, 11).
  - [TRACK_D_ALPHA_GI.md](TRACK_D_ALPHA_GI.md): alpha test, hybrid RT GI, effect catalogue (sections 7, 8).
  - [TRACK_D_LOCAL_LIGHTS.md](TRACK_D_LOCAL_LIGHTS.md): D-LS local-light shadows, box lights (section 9).
  - [TRACK_D_REFLECTIONS.md](TRACK_D_REFLECTIONS.md): D-RF reflections, unknown queries (sections 10, first 13).
  - [TRACK_D_SUN.md](TRACK_D_SUN.md): sun replace, current native-caster sun scene (section 12).
  - [TRACK_D_EMISSIVE.md](TRACK_D_EMISSIVE.md): D-EM emissive geometry (section 13.1 to 13.21).

## Designs and plans

- [P3_BOX_LIGHTS.md](P3_BOX_LIGHTS.md): box lights integration candidate.
- [P4_OMM.md](P4_OMM.md): opacity micromaps: predicate, EXT backend, GPU producer, content epochs.
- [P6_WATER.md](P6_WATER.md): water design for coverage gap 1.
- [NATIVE_HAIR.md](NATIVE_HAIR.md): native CHAR_Hair/Pubes coverage, depth and the physical secondary origin.
- [NATIVE_TILESET.md](NATIVE_TILESET.md): native tileset depth coverage.
- [SUN_CURRENT_SCENE.md](SUN_CURRENT_SCENE.md): current native-cascade sun candidate.
- [DIALOGUE_SHADOWS.md](DIALOGUE_SHADOWS.md): dialogue face shadows and distant casters.
- [OPAQUE_TWO_SURFACE_REFERENCE.md](OPAQUE_TWO_SURFACE_REFERENCE.md): bounded two-surface opaque lighting reference.
- [RTX_HAIR_FEASIBILITY.md](RTX_HAIR_FEASIBILITY.md): RTX Hair feasibility study.
- [PT_FEASIBILITY_OFFLINE.md](PT_FEASIBILITY_OFFLINE.md): path tracing feasibility and rendering ceiling.
- [DESIGN_SOTA_NEXT.md](DESIGN_SOTA_NEXT.md): study of the next three state-of-the-art items (many lights, radiance cache, OMM).
- [CONSOLIDATION.md](CONSOLIDATION.md): repo consolidation plan of 2026-10-31.

## Testing and QA

- [QUALITY.md](QUALITY.md): quality rules R0 to R10 and the per-effect audit table.
- [QA.md](QA.md): the image-quality QA harness (`python -m scripts.qa`).
- [TEST_SCENES.md](TEST_SCENES.md): the test saves S1 to S14 and what each one stresses.

## Archive

[archive/](archive/) holds superseded docs, kept for history. Do not treat them as current:
old audits (AUDIT_ENGINE_DATA, AUDIT_RT, AUDIT_SOTA), ROADMAP, STATUS, research.md,
closed phase notes (P1_PAN_FLASHES, P2_*, P3_NATIVE_DIRECT_VALIDATION), DEVICE_LOST_DLS,
SUN_PENUMBRA_REVIEW, UI_CONTAMINATION_OFFLINE, the merged OMM and hair-origin notes, DISK_HYGIENE
(the 2026-10-30 storage audit, done on 2026-10-31),
GOAL_RENDERING_AGENT and the full progress history PROGRESS_CODEX_2026-10-31.md.

## Naming legend

The same letters mean different things in different docs:

| Name | Meaning | Defined in |
|---|---|---|
| P1 to P6 | Old work phases (before 2026-10-30): P1 motion/pan correctness, P2 pending merges and emissive (D-EM), P3 box lights, P4 OMM, P5 SHaRC v2 and light tree, P6 water | archive/PROGRESS_CODEX_2026-10-31.md ("Next phases"); doc names P3_, P4_, P6_ |
| M0 to M6 | Milestones of the current rendering program (replace the P phases) | RENDERING_EXECUTION_PLAN.md |
| S1 to S14 | Test scenes (player saves) | TEST_SCENES.md |
| R0 to R10 | Quality rules (R0 physically correct, R5 validation, R7 cost budget, ...) | QUALITY.md |
| R1, R2, R3, ... after a feature name | Numbered live runs of one feature's test batch, for example "GI R3", "material R1/R2", "R8" in the plan's milestone table, D-EM R4/R5 | PROGRESS_CODEX.md, HANDOFF_SESSION.md, RENDERING_EXECUTION_PLAN.md, TRACK_D_EMISSIVE.md 13.20 |

So "R3" alone is ambiguous: write "QUALITY R3" for the rule and "GI R3" for the run.
