# BG3RTX progress - 2026-11-02 evening (ABI 26)

Current summaries below. Full folded text, verbatim: [evening archive](archive/PROGRESS_20261102_evening.md).
Earlier history: [morning archive](archive/PROGRESS_20261102_morning.md), [11-01 evening](archive/PROGRESS_20261101_evening.md),
[11-01](archive/PROGRESS_20261101.md), [11-01 Codex](archive/PROGRESS_CODEX_2026-11-01.md), [10-31 Codex](archive/PROGRESS_CODEX_2026-10-31.md).
Branches write `docs/progress/<branch>.md`; fold only after verifying ancestry against main.
Resume: [HANDOFF_SESSION.md](HANDOFF_SESSION.md). Stage order: [GOAL_NEXT.md](GOAL_NEXT.md).

## Report 2026-11-03 morning (main e8984b6, blackout ~06:20: ds2/ds3, ba1, om4 re-run) - stages 2-3 closed, 4 in progress
- Stage 2 done: spec-constant migration on main; live scc1 valid (0 DXC on every converted toggle, rt 1.86 ms, gi 5.0 ms S8). Variants 2946 -> 147.
- Dialogue moving shadow (port): fixed by default. Second fix (cascade identity without per-draw allocations) now matches 16-18 duplicates/frame at the port (was 0); no band in any motion pair (block change at RT-sun-off noise). rt_cascade_static_only must stay OFF: it drops every cascade caster (same as casters off).
- AO / hair at camp confirmed: gi_ao=0 brightens faces +10..22%; native hair +4..14%.
- Water on/off reflections while turning: cause in our compose (tier 1 25% -> 1% on camera-jump frames). Compose shift (rt_water_compose_shift) did not remove it; test confounded by jumpy turn input (45-90 deg per frame). Smooth turn modes + camera probe job prepared (bundle 2).
- Native hair: +2.3..4.5 ms rt / +3.4..6.9 ms GI (S8, camp dialogues): stays off. rt_hair_gi_cull saves 1.2-2.3 ms but brightens characters +9..20% (not equivalent). Bug: Dialogue3 with hair fills the mesh pool (2047 MB), scene then skipped. Branch hair-cost2: pool fix, cheaper coverage, near-only hair.
- Characters dark in dialogues: RT AO self-occlusion (50% of character pixels -39%, 99% self-hits under 5 cm). Branch ao-self-occlusion: derived ray start for characters.
- Box lights: honour the game's shadow flag (88 Grymforge box lights have it off): fixed mode is closer to OFF energy, +0.13 ms. Still off (your visual check, question below).
- OMM om4: check window confounded by scene-wait timeouts; gate fixed, rerun pending. rt passes -0.4..0.5 ms with OMM.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa (147 -> 135 variants). rf_edge / rr_hitdist kept (old metric invalid).
- New, off: water hidden/off-screen hit shading with world-space lights (rt_water_hit_shade 1/2), mesh content ids (rt_mesh_content_id).
- Try in game: port dialogue (Dialogue3 save): the shadow band on the still character should be gone with Roof sun shadows on. Grymforge with Box light shadows = All: are the art lights back? If yes, it can go to Ultra.

## Report 2026-11-02 19:30 (main 7929040, ABI 26) - consolidation paused, PC freed for the user
- Merged: stream caps + source trim (-0.4 GB VRAM), NaN GI history fix, OMM option B + VT alpha masks (game-wide catalogue 1,677 materials, generated on the user's PC), recipe rows from the asset DB, water RR/FG motion vectors, preprocessed-text shader cache (header-only edit: 2892 compiles to 0, 35 s), AGENTS.md compile rules. All new keys off.
- Rejected / stays off: IP grass (+1.9 ms, +1.5 GB, +3 points share), generated recipe rows (+2.3 points camp), every water flicker key. rf_hit_gi off (hit AO matches the screen offline; gap is in instanced hits, needs a pack with instanced rows).
- Water flicker at the port: closed, it is the game's own water (wf3-wf7: our compose, RR, sun-shadow ray, glints and FG all excluded; with RT the water is calmer, 0.54 vs 0.77-0.86).
- Dialogue shadows: RT AO darkens characters most (AO off +18..42%); at the port opaque hair cards add to it (native hair +27%). Hair cards are not alpha-tested in RT at all (no CHAR_Hair shader in alpha_fs.txt).
- OMM: live upper bound camp ~5.7-6.3 ms (forced opaque). Masks cover ~97% of camp alpha candidates. Dynamic micromap bake built (rt_omm_dyn, worktree-agent-a53f0b584fa39317b), not yet run: om4 waits.
- In progress (user rule: finish the build improvement before more big compiles): spec-constant migration on `variant-migration` (G guards + S2a done; 2986 requests on main to ~980; target ~100-180). Then one merge-green, live scc1, then rebase and run: occ diagnostic + dialogue batch, native hair (async compile, water hair) + hair_qual, om4, m4_generated rerun.
- Try in game: S8 port courtyard pool is darker with RT (our sun shadow removes glints in shade): say if you prefer a softer water shadow edge. Native hair at the port dialogue should brighten characters (first enable still compiles until the hair branch lands).
- Questions: see below (ls_box=2 visual check, GUI_AUDIT_20261102 five yes/no, project licence, NVIDIA notification).

## Report 2026-11-02 04:10 (main fe8f75d)
- Done: stage 1 live (smoke_ultra valid with every Ultra key; scene wait 0 timeouts in gated windows; water 120/120 composed; shader cache 2nd launch 44 read 0 compiled; FG letterbox 2560x1440 in 3840x1600 swapchain FG 2.00 -> fg_letterbox automatic). Stage 2 GUI: Experimental 28 -> 5 entries, levels for water/materials.
- Fixed: traced frames skipped the hit-material capture (layer/geocapture.cpp), so all earlier traced energies of hit features measured them off; re-measured after the fix.
- Ultra now: + Bounce, Emission at ray hits (Hidden hits), Far-field sun bounce 6 m (gi_far: S8 -2.01% vs predicted -2.0%, camp -2.42%; +3.2-3.7 ms GI trace). smoke_ultra with all: valid, median frame 16.65 ms (was 13.75).
- M6 step 1: native ambient / PT reference S8 1.44, camp 0.84, inn 0.78: not consistent, no global correction.
- M4: S6 diagnostic: GI hit shading adds light (+1.6% pack invariant), no darkening. Alpha-tested static meshes get material rows (rt_hit_material_pool_alpha, live-tested S6).
- Box lights ls_box=2: energy within band, +0.32 ms S6 / +0.20 ms S7. Needs your visual check (question below).
- Open: rf_hit_shade stays experimental (bias); lava flowmap and camp grass coverage; SHaRC; release hardening (stage 6).
- Try in game: Ultra preset at Water Queens House (S8) and camp: softer far-field bounce darkening under roofs/foliage, emissive lava light in the Adamantine Forge.
- Question: ls_box=2 in the S6 save (Adamantine Forge): the lower-right walkway right of the front disc and the central pillar should darken slightly (ledge shadow). OK to turn it on in Ultra?

## User questions and decisions

- Open: **ls_box=2 in Ultra?** (question above). Until you answer, Box light shadows stay Off in every preset.
- Open: **GUI_AUDIT_20261102** five yes/no decisions; **project licence**; **NVIDIA notification** (Streamline/NGX).
- Open (look): water sun-shadow edge: soften (more rays / temporal visibility) or keep game glint in shade?
- Decided 11-02: OMM path delegated (option B); assetdb and VT masks generated on the user's PC, not shipped;
  gi_reproj guards live approved (now as spec constants); dropped Codex refs may be deleted; packaging paused;
  no compile time on work a planned build improvement makes unnecessary.
- Open (not blocking): **`rt_hit_emission` 1 or 2** (1 = hidden hits only, 2 = also screen hits). Ultra uses 1;
  2 is developer only.
- Decided: **`rf_hit_shade` stays Experimental until its bias is within the game's own spread** (tier (a)
  L_b / L_a S8 0.925, S6 1.089, camp 1.097). The threshold is not lowered.
- Decided: **`ls_k` stays off** in every preset (developer only).
- Decided: GUI audit approved (docs/GUI_AUDIT_20261101.md): levels instead of single keys; emission writes `rt_emissive`.
- Decided: crash dumps stay off for players. Later a diagnostic mode builds a package (logs, effective settings,
  GPU info, crash dump) with a save the user picks, copied read-only; it never reads saves on its own.
- Decided: disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.

## Ultra preset and GUI (stages 1-3 done)

- Ultra: RT water "+ Refraction", hidden-surface shading "+ Bounce", emission "Hidden hits", far-field bounce 6 m.
- Costs: water chain 0.53-0.67 ms; hit emission +0.08 ms; gi_far +3.2 ms S8 / +3.7 ms camp GI trace.
- smoke_ultra: every-window gates pass, water 120/120 composed, scene wait 0 timeouts; median frame 16.65 ms.
- Automatic: hit-material recycle, unlocked capture, lean hooks, scene wait, FG letterbox and pool alpha rows.
- GUI: Experimental 28 -> 5 entries; water, shading, emission and far-field use levels. Old user INIs migrate.
- Box shadows, + Reflections, native coverage, hair origins, OMM and cutout spill remain outside Ultra.

## Hit-material coverage and memory (stage 4)

- Traced frames now retain hit-material capture. Earlier traced energies measured these features off.
- Pool alpha rows are automatic: S6 qualified 14.8% -> 52.1%, energy +1.06% / -0.38%, GI trace +0.46 ms.
- Wind RGB S8: bit-exact, rejected 0%; qualified 45.0% -> 63.5% with all three instanced families;
  energy -0.00% (band 1.30%), trace +0.67 ms. Kept in Ultra.
- Mask rows: S8 +0.65 points qualified, energy +0.04%, trace +0.006 ms. Inn re-gated valid: +12.1 points
  (above predicted 0.5-6), energy -0.80% (band 1.20%), trace +0.92 ms. No promotion recorded.
- Stream caps follow capture size: Ultra source 384 MB, deferred 13 x 32 MB, query basis 768 MB; allocated by use.
- Retired source copies return to the pool; consolidation report records about 0.4 GB saved.
- IP grass stays off: +3 points qualified, energy +0.19% (band 2.19%), +1.9 ms and about +1.5 GB after trim.
- Generated recipe rows stay off (+2.3 points camp); bush pairing / two-sided rerun pending in the unmerged lane.
- Inn capture split now follows alpha/opaque demand when reserves do not fit: 541 / 483 MB in the failing case.
- Missing recipes: animated lava flowmap, Tileset vertex COLOR, HeightGradient world height; see PLAN_M4_REFLECTIONS_GI.md.

## Reflection shading and GI history

- rf_hit_shade stays Experimental: bias above the game's spread, +1.4-1.8 ms. rf_hit_gi remains off.
- Offline S6 light visibility did not change with alpha footprint x1/x16/x256 (55.8% lit); no visibility fix justified.
- Reflection replay is unavailable. Prepared rf_hit_gi_ab measures S6/S8 ratios, energy and cost in one run per scene.
- GI history NaN/Inf fix selects the fresh sample directly; non-finite reprojection is rejected before integer conversion.
- NGX motion-vector scales update only when both are finite. Finite inputs retain the same behavior; layer needs restart.
- Motion at the depth sample and off-screen previous-radiance rejection were not recovered; they need gated A/B work.

## Water and dialogue shadows

- Water flicker attribution is closed by the top report. Every experimental water flicker key stays off.
- rt_water_rr_hitdist was null in the corrected same-direction test; rt_water_rf_edge worsened water flicker by 42%.
- wf3 VALID: compose off 3.53% transient rate, between OFF repeats 3.38% / 3.61%; compose cost 0.41 ms, 0 when off.
- Water energy uses the pre-water scene plus the in-frame compose delta; the earlier dump preceded compose.
- Dialogue AO and opaque hair findings are in the top report. Native hair async compilation and water variants await their lane.
- In game: S8 pool columns reflect and the bed shifts with the view; S3 pond has tree shadows. Compare via Ctrl+Shift+F9.

## Local lights

- ls_box=2 stays off pending the user's S6 visual check. S6/S7 cost +0.32 / +0.20 ms; energy within each repeat band.
- S6: 5.45% of lit luminance removed, 18.0% of 369M rays occluded. Look at the lower-right walkway and central pillar.
- ls_k stays off. em_forge sliced check max 0.026 ms (<1 ms); a rerun would supply a formal receipt only.

## Far field, SHaRC and ray count (stage 5)

- No global ambient correction: native/PT ratios S8 1.44, camp 0.84, inn 0.78 are inconsistent.
- gi_far 6 m stays in Ultra: S8 -2.01% vs predicted -2.0%; camp -2.42%. gi_debug=2 saturates; do not use for energy.
- SHaRC sh1 failed: energy -0.86% vs predicted +1.5%; 23.8% found, 10.1% occluded-cell hits, about +0.75 ms total.
- SHaRC stays off: cache radiance undercuts qualified hidden hits. Offline single-frame packs lack its history.
- One GI ray: energy +0.11% (band 1.47%), trace 4.32 -> 2.02 ms. Flicker comparison INVALID: OFF/OFF null failed.
- sh2 rerun prepared with six traces per state. Keep two rays in Ultra until the null and excess-noise gates pass.

## OMM and game data

- VT masks: 65 masks / 79.2 MB added for camp; 99.4% VT triangles, about 97% shadow / 98% visibility candidates covered.
- Offline alpha agreement 99.05%; two sub-tile textures (13,092 triangles) retain the page cache.
- va1 VALID: masks alone neutral, energy +0.24% (band 1.95%), unchanged GPU cost; coarser-page share did not fall.
- Catalogue: 1,677 materials, 125 templates, 1,174 textures; 1.3-1.7 GB generated on the user's PC, not shipped.
- rt_vt_alpha_miss_log ranks missing masks. First-install design: P4_OMM.md 11.2h.
- OMM stays off. Guard checks moved out of per-ray host atomics; dynamic bake om4 still pending, as reported above.
- GAME_DATA_AUDIT.md ranks exact-data gaps: masks, generated rows, caster roofs, vertex COLOR and class 4 transmission.

## Gates, tooling and release (stage 6)

- Snapshot gates are events with 1 s settle; layer stats print on the first completed query per 120-frame window.
- Every ON window must pass. smoke_ultra forbids variant skips, roof warnings, guards, basis overflow and pool-full lines.
- Long-session read: VRAM flat from cycle 13 (18.2-18.7 GB); records 411-417; no unbounded leak observed.
- AS chunks retain 4.2 GB for 0.25-1.4 GB live AS: fragmentation remains open. Trend baseline now uses cycles 12-14.
- Preprocessed cache: header comment edit 2892 compiles -> 0 in 35 s; self-check 4/4 identical. Prune cap follows protected size.
- Settings generation omits the conflicting key-count comment; tables unchanged. No shader request changes in this fold.
- A/B latency logs toggle to present of frame F+2; ab_off exposes the same toggle to queue jobs.
- Release still needs long-session qualification, acts 2-3, installer checks, README, licences and diagnostic packaging.
- release.ini forces rt_emissive=0; revisit with ls_box now that emission is in Ultra. DXC licence text remains missing.
- Pending lives need explicit batch steering after their dependent offline checks. Nothing here grants live permission.
