# Codex branch triage (2026-11-02, main 566db3d)

Scope: the 32 local `codex/*` branches not merged into main, plus `sup/cascade-alphatest` and
`lscheck-fix`. No branch was deleted, moved or force-pushed.

How "status on main" was decided: `git cherry main <b>` (patch-equivalent commits), then, for
every line a branch adds outside `docs/`, whether main's file at the same path holds the same
line (scratch script; "code x%" below). Low numbers were then read by hand: main often has the
same feature in a later form (wrapper shader trees became defines, `layer/omm_classify.h` became
`common/omm_classify.h`, the EN2 hit material became `rt_hit_material`). Most of these branches
were already reviewed in docs/CONSOLIDATION.md E2/E3.

Status: **superseded** = main has it or a later form of it; **partly** = some of it on main;
**absent** = not on main.

## Table

| Branch | Commits | What | Status on main | Value | Recommendation | Reason |
|---|---|---|---|---|---|---|
| codex/camera-native | 77 | Native camera metadata transport, GPU camera probe, FG composite metadata, swapchain registry, radiance-depth pairs, owned motion (P1 research, 10-28) | absent (code 1%) | low: diagnostics | drop (keep ref) | FG/swapchain/provenance research threads; P1 closed without a gain. Two small fixes extracted (rows below the table) |
| codex/camera-schema-wip | 21 | WIP of the above: owned motion vectors, private checkpoints, MV jitter | absent (1%) | low | drop | Subset of camera-native, WIP |
| codex/en2-audit-preserved-ab8aafd | 15 | EN2 `rt_hitmat`/`rf_hitmat` material at secondary hits, FG resize diagnostics, temporal QA attribution | absent (1%) | none now | drop | Superseded in substance by `rt_hit_material` families (ABI 25); FG resize is research |
| codex/fg-composite-metadata | 66 | FG composite metadata, resize diag, vertex probe, owned motion | absent (1%) | low | drop | Same research stack as camera-native |
| codex/fg-vertex-probe | 56 | Bounded GPU vertex probe of the FG composite + the same stack | absent (1%) | low | drop | Same research stack |
| codex/research-preserved-20261028 | 81 | Snapshot of the 10-28 research tip (camera, FG, motion) | absent (1%) | low | drop | Preservation ref of camera-native |
| codex/p1-pan | 108 | P1 pan: GI history surface gates (fresh, diffuse exact, geometric normal, normal footprint), exposure hold, owned origin | absent (1%) | low | drop | Its own last commit: "without claiming a quality gain"; energy gates failed (GI_*.md on the branch) |
| codex/mv-jitter-optin | 4 | Opt-in scene-matched MVs from the velocity first draw, NGX scale checks | absent (2%) | low | drop | P1 research; the MV scale finite check is recovered (recover-gi-history-nan) |
| codex/hair-capacity-fix | 3 | Despite the name: queue child setup rejects a game appearing, ordinal build freeze, file hash without a PS module | superseded (100%) | none | drop | All 3 patches in main |
| codex/hair-physical-origin | 4 | `rt_hair_physical_origin`: physical secondary-ray origins for native Hair, finite-light centre kept, needs native Hair raster receivers | superseded | none | drop | Key, GUI entry (Experimental "Physical hair ray origins") and NATIVE_HAIR.md section on main; wrapper trees became defines. Live 10-31 (archive PROGRESS 623): Hair sun visibility 0.150 -> 0.355 -> 0.164 off/on/off, RT 3.44 / 3.41 ms |
| codex/hair-origin-live | 5 | hair-physical-origin + USER_PLAYING markers from private queue roots (047e105, the user's trial build) | superseded | none | drop | All patches in main. The user trial on 047e105 reported camp rectangles and a hairline band (archive PROGRESS 26-47): open for the hair stream |
| codex/hair-receiver-ab | 1 | Runtime A/B key `rt_hair_receiver_guard` for the physical receiver guard | superseded (patch in main) | none | drop | Evidence JSON says "diagnostic, no quality verdict" |
| codex/hair-scalp-receiver-guard | 2 | Guard: keep virtual Hair origin when the Scalp receiver is ambiguous or the source geometry unreadable | superseded (patches in main) | none | drop | 31 CPU checks, 16 off-identical / on-valid variants (its evidence); no live gate |
| codex/m2-foundry-mesh | 21 | Offline foundry GR2/terrain reconstruction, terrain packet readback, SE console probe for Blackbody | superseded (92%) | none | drop | Main's terrain_packet_dump.inl is a later version (shader copies, lifetime) |
| codex/m3a-vt-material | 4 | Native VT surface field decode, sampled-view retention in F10 traces, RTX Hair feasibility doc | superseded (98%) | none | drop | Files on main in later form |
| codex/native-coverage | 12 | Native Hair/Scalp/Pubes coverage capture, OMM writer epochs | superseded (99%) | none | drop | f9d5b86 (alpha_capture_source) is on main (layer/alphatest.cpp:763) |
| codex/native-hit-material | 5 | Match audited native hit draws by vertex inputs; GPU entry/publication stats | partly (83%) | low | drop | native_hit_inputs.h is on main in a later form; a15f5a3 only adds rtStats 269..271, main's table is far past them |
| codex/native-tileblend-camera | 5 | Exact TileBlend camera coverage | superseded (93%) | none | drop | On main |
| codex/native-tileblend-shadow | 7 | TileBlend shadow material in the current-Sun path | superseded (97%) | none | drop | On main |
| codex/native-tileblend-legacy | 9 | "Prepare" a material-aware TileBlend legacy cascade bridge (native_legacy_cascade.h) | partly (3%; the 6 base commits are on main) | low | drop | Unfinished prep; main has its own legacy cascade copies (geocapture.cpp c_legacy_copies, cascade::legacy_caster) |
| codex/native-tileset-coverage | 2 | STI material capture layout, family stats, `rt_native_tileset` | superseded (19%: the rest is 600 obsolete wrapper files) | none | drop | `rt_native_tileset` on main (62 files) |
| codex/native-tileset-st-depth | 2 | Native tileset depth for the exact static recipe | superseded | none | drop | Same patch reached main through the tileblend branches |
| codex/native-transport-parameters | 1 | Map native class 0 fields to shared transport parameters | superseded (96%) | none | drop | Patch in main |
| codex/transport-fresnel-blend | 3 | FresnelBlend transport BSDF with native adapter | superseded (100%) | none | drop | Patches in main |
| codex/omm-ext-backend | 5 | OMM retired-slot feedback, cache ownership, native sun math | superseded (100%) | none | drop | On main |
| codex/p4-omm | 1 | Conservative OMM interval classifier + test | superseded | none | drop | Main has it as common/omm_classify.h (docs/P4_OMM.md 68) |
| codex/opaque-two-bounce-reference | 1 | Bounded two-surface opaque point/spot reference (`rt_hit_reference`) | superseded (63%) | none | drop | `rt_hit_reference` on main (59 files); the rest is old wrapper files |
| codex/tileblend-gpu | 1 | Windowless GPU validation of TileBlend two-texture coverage | superseded (100%) | none | drop | On main |
| codex/vegetation-pan-draft | 2 | K16 pan bursts after warmup, cascade support live-test draft | superseded (100%) | none | drop | On main |
| codex/queue-cancel-review | 5 | Cooperative queue cancellation, explicit batch steering | superseded (99%) | none | drop | Patches in main |
| codex/queue-child-context | 4 | Validate the consumed queue context before each auto-run launch | superseded (100%) | none | drop | Patches in main |
| codex/queue-live-batch | 2 | Batch steering + qualified-scope experiments in settings/launcher (`rt_sun_cascade_current`) | superseded (80%) | none | drop | Patches in main; settings GUI was reworked since |
| sup/cascade-alphatest | 3 | `rt_cascade_alpha=3`: alpha-tested cascade casters (one BLAS geometry + record each) | superseded | low | drop | Main's current-Sun path (`rt_sun_cascade_current`, geocapture.cpp 1306-1330) snapshots native cutout materials of cascade draws; live jobs sup-calpha-in/out ran 10-30 but were never analysed. Re-check only if current-Sun is dropped |
| lscheck-fix | 2 | D-LS frozen reference mirrors the live ray class, guards and caps | superseded (92%) | none | drop | Main's ls_ref_body.hlsli has the class, guards, cap and the tri-state on top |

Counts: 34 branches. Superseded 24, partly 2, absent 8 (all research stacks). Recover 0
branches whole; 2 small fixes extracted from the absent research stacks (below). Needs live:
none of the branches as they stand. User decision: none needed for the branches; deleting the
refs (CONSOLIDATION E2) stays the user's call.

## Extracted fixes (from research stacks, not on main)

Ranked by value now:

1. **GI temporal NaN/Inf safety** (from 06f62f9, camera-native/research-preserved/p1-pan) and the
   **finite NGX MV scale check** (from 9078be6, mv-jitter-optin). Main's gi_temporal.hlsl writes
   `lerp(hist, cur, 1)` when the history is rejected: a non-finite history texel gives NaN again,
   so it never recovers. Main's ngx.cpp stores MV_Scale_X/Y without checking the read or
   finiteness. Pure fix, value-identical for finite inputs. -> `recover-gi-history-nan`.
2. **GI temporal motion at the depth sample pixel and out-of-image rejection** (06f62f9): main reads
   the motion vector at the fixed pixel `id*2+res_shift` but the depth at the rotating sample
   `gi_sample_px(id)`, and floors the reprojected position before any finite check. Changes the
   image in motion: needs a define variant (common/shader_variant.h, variants.txt) behind a new key,
   and a live pan A/B. Not done in this pass.
3. **Previous-radiance reprojection fallback** (f32657e): main's `prev_radiance_px` returns the
   current pixel when the reprojected one is off screen, so GI bounce and tier-a reflections read a
   different surface's last-frame radiance at the screen edge during pans. The fix rejects instead
   (falls back to the next tier). Image change at edges: needs a variant key in the files that
   include gi_common.hlsli, and live. Not done in this pass.

Dropped from the research stacks as not applicable to main: partial swapchain enumeration
(1689d65; main's tracker already accepts VK_INCOMPLETE), BVH reset on command buffer reset
(7c84ceb; main's cmd.cpp calls bvh_cmd_reset on reset, free, begin and pool reuse), EN2 G-buffer
join wait (0916cff; EN2 hitmat is not on main).
