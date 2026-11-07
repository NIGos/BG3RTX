# RTX hair: plan to a complete implementation (2026-11-03)

User request: a complete RTX hair that uses every strand capability BG3 allows. This plan supersedes the closure plan
(docs/RTX_HAIR_PRIMARY.md section 8, branch claude/hair-reload-fix a21262e0) and adds the items that plan left out.
Design detail stays in RTX_HAIR_PRIMARY.md, RTX_HAIR_ROADMAP.md and RTX_HAIR_INTEGRATION.md.

## Rules for this plan
- Every runtime change sits behind its key, off by default, with key-off identity (or a REFERENCE=0 proof), and is
  tested in smoke_ultra with the other qualified keys on before it can ship.
- Offline first. Each milestone ends with one live confirming run whose predicted numbers are committed first
  (predicted.json). Lives need the user's go per batch.
- No full style rerun until every converter or reference change is in (user, 2026-11-03). Between those changes,
  validate on the representative set only (M1).
- Gates are derived, never loosened without a reason derived from the data.

## Where it stands (updated 2026-11-04; this plan is the source of truth)

2026-11-04: hv9 hair visual is in progress; strands remain a post-release Experimental update by user decision. Stock visible hair ships.
Closure-plan steps (RTX_HAIR_PRIMARY.md section 8) inside the milestones: C1 = M0, C2 = M2.1, C3 = M3.1-3.3,
C4 = M4.1, C5 = M1 (+ M7 content), C6 = M5, C7 = M8.

| Milestone | State | Commits / branch | Live |
|---|---|---|---|
| M0 stability (C1) | ready: strike rule, header order + A/B in hv5, strand-draw gates | 51956f86, f659b5ee, 4661e4bc (bundle12) | L1: hv5 + hair_crowd after bundle12 |
| M1 conversion (C5) | done offline: converter 2.4-3.4x, renders 1.8x (bit-identical), caches, 80-style set, fibre reference, radial orientation, root from geometry; LOD1 transfer rejected by its gates; full rerun done: 877 pass / 295 review / 45 reject (was 745 / 346 / 126); 2-D curve tracer for curly atlases (converter 2, batch 5): representative set + HAIR_Afro_B 50 / 60 / 9 -> 69 / 47 / 3, Afro_B 0 / 38 / 4 -> 17 / 25 / 0, other styles bit-identical; next full rerun justified (~1 h, 4 jobs) | 068ff6e4 .. 71de9bc1 (claude/hair-batch-lod), claude/hair-m1-curly | none (counts offline) |
| M2 shadows (C2) | M2.1 transmittance done offline (spec 1026, in main); M2.2 audited: cascade fallback retains card shadows, safe replacement not implemented ([analysis](RTX_HAIR_SHADOW_MAPS.md), [audit](progress/claude-hair-cascade.md)); M2.3 AO transmittance done offline (spec 1044, rt_hair_groom_ao_t; RTX_HAIR_PRIMARY 8 "M2.3") | 048d60d5 (claude/hair-shadow-transmittance); claude/hair-m2-m4-light | L2; M2.2 not ready |
| M3 image (C3) | M3.1 tips done offline (rt_hair_groom_tips); M3.2 two-ray coverage done offline (rt_hair_groom_coverage, spec 1042: unbiased, box error -4 to -7%, 2x strand trace); M3.3 RR hair guides done offline (rr_hair_guides, spec 1043: class 5 metalness 0, fibre F0); M3.4 post depth not started | 795f6008 (claude/hair-tips), claude/hair-m3-image | L2 |
| M4 indirect (C4) | M4.1 diffuse done offline (rt_hair_fibre_gi, in main); M4.2 specular done offline (rt_hair_fibre_spec, pass after D-RF; RTX_HAIR_PRIMARY 3 "M4.2"); M4.3 mapped local lights done offline (rt_hair_fibre_mapped, spec 1030; RTX_HAIR_PRIMARY 3 "M4.3"); M4.4 skipped (SHaRC removal planned) | claude/hair-m2-m4-light | L3 |
| M5 views (C6) | not started | | L3 |
| M6 performance | not started (SER applies to ray-tracing pipelines, not to the inline queries the strand passes use: half-resolution traces and the crowd budget are the levers) | | L4 |
| M7 content | beards and hat hair through M1's rerun; dyes need a live check | | L4 |
| M8 qualification (C7) | not started | | L5 |

## Milestones (in order; two to three streams run in parallel, one agent per stream)

### M0. Stability on screen (C1). Stream A. Live.
Merge bundle12, rebase C2 on main. One run: hv5 (reload bisect + header-order A/B) and hair_crowd.
Gate: no device loss; strand draws > 0 in every strand window; the adjacent-tag hypothesis decided by the A/B.
If it is confirmed: rt_hair_groom_primary_order becomes the default path of the key (then qualified in M8).

### M1. Conversion pipeline, faster and complete. Stream B. Offline, then one full rerun.
Measured per style (Shadowheart / Gale's beard / Halsin): convert 8.4 / 4.8 / 12.6 s, dedupe 5.9 / 4.0 / 12.5 s,
renders 7.6 / 1.7 / 10.3 s, GR2 dump 2.7 / 1.5 / 5.0 s; 22 s CPU per style on average, 1217 styles in about 2 h.
1. Speed:
   - cache each style's converted groom (npz, keyed by its inputs and the converter version), so a reference or
     gate change re-validates without converting again (saves the convert and dedupe share, about 60%);
   - cache the GR2 dumps (keyed by the pak entry);
   - vectorise `convert.locate` (one Python loop per triangle) and replace `geom.pairs_within` in dedupe with a
     uniform grid hash;
   - target: under 8 s per style, a full rerun under 45 min.
   Done (2026-11-03): converter 2.4-3.4x faster and renders 1.8x faster, both bit-identical on three real styles;
   GR2 and groom caches with --revalidate; the 36-style sample 201 -> 118 s cold with identical numbers.
2. Representative set (`--names`), used between changes:
   - every style drawn in the live test saves: index counts from the "[hair groom]" lines of the Dialogue1, Campsite
     and S8 logs, mapped to styles by triangle count;
   - every companion style;
   - three styles per remaining failure class.
   About 120 styles, about 10 minutes. Done: tools/hair_groom/sample_names.txt, 80 styles (logs, every companion,
   three per failure class, LOD siblings).
3. Converter items that need the rerun:
   - LOD1 grooms transferred from the passed LOD0 groom: tried and rejected (2026-11-03, branch
     codex/hair-groom-lod1, docs/evidence there): the LOD1 cards are thinner than the LOD0 hair by design, so the
     transferred LOD0 density over-covers them (Gale's beard LOD1: coverage +0.25, IoU 0.68 -> 0.47; Shadowheart
     LOD1 pass -> review) while the own LOD1 conversion already passes ordinary hair (Shadowheart LOD1 IoU 0.94).
     LOD1 keeps its own conversion;
   - a 2-D curve tracer for curly atlases whose curls do not run along the rows (HAIR_Afro_B: 32 styles fail the
     root channel). Done (claude/hair-m1-curly, tools/hair_groom/curves2d.py): atlases whose root channel does not
     follow the rows (median root/row correlation < 0.3, the old reject threshold) are traced along each strand, through
     crossings by direction, curvature and the R (ID) channel; strands ordered root to tip by B; the root check is the
     same correlation along the traced strands. Afro_B 0 / 38 / 4 -> 17 / 25 / 0 (left: silhouette IoU 0.79-0.85 of the
     B helmets, as before, and hat hair with roots on the helmet rim); the Bugbear beard (corr 0.24) reject -> pass;
   - the fibre reference (done, 068ff6e4).
Gate: the representative set first, then one full rerun with the counts per category in RTX_HAIR_PRIMARY 5a.

### M2. Shadows. Stream A. Offline, then live.
1. Transmittance through strands (C2b, done offline): rebase, then live.
2. Consistency with the game's sun shadow maps: they still render the cards. Where the RT shadow does not replace
   the game's, hair casts the cards' shadow. Either the strands' visibility replaces the hair term of the game's
   shadow on receivers, or the cards are kept out of the game's cascade when the groom is live. Read the existing
   shadow design (TRACK_D_RT 6.7, cascade casters) first.
   Audit 2026-11-04: Ultra does not bypass the native mask on all character receivers. Roof volumes,
   partial/off-screen trust and unknown rays keep it (`gi/rt_shadow.hlsl`, `gi/rt_sun_resolve.hlsl`).
   A shared cascade draw cannot be removed using groom-live membership alone: fallback receivers would lose
   its shadow. Receiver-side replacement needs caster attribution plus a complete visibility certificate;
   the current `SunCascadeDraw` has no groom draw key. No rendering change or live-ready candidate yet.
3. Ambient occlusion from strands on skin, with the same transmittance (today strands occlude AO opaquely or not
   at all, depending on the pass).
Gate: skin and neck under hair not black and not unshadowed; key-on vs key-off energy within noise on a static
camera; PT reference on the bench fixture.

### M3. Image quality. Stream A. Offline, then live.
1. Tips: the game's dithered alpha test at the strand's own mask alpha (root channel near the tip), so tips fade as
   on the cards (C3).
2. Sub-pixel coverage: strands thinner than a pixel rely on the jittered camera ray plus TAA/RR. Measure the flicker
   with the edge-flicker detector; if it is above the cards, use two rays per strand pixel or a coverage estimate
   from the capsule distance.
   Done offline (2026-11-07, RTX_HAIR_PRIMARY 5d): two stratified rays (the footprint estimate needs a widened
   second BLAS). Unbiased; 3 x 3 box error -4 to -7% against an 8 x 8 area reference, at twice the strand trace.
   Binary G-buffer pixels bound the gain; L2 decides with the edge-flicker detector.
3. DLSS RR guides: hair material class, specular albedo and roughness of the fibre, hit distance; qualify the "RR
   hair resolve" candidate with strands.
   Done offline (2026-11-07, RTX_HAIR_PRIMARY 4): class 5 guides read RT2.z as backlit, not metalness (it fed RR a
   metal before); fibre F0 0.0465, roughness >= 0.09. Hit distance stays D-RF's kNoHit on hair (not traced).
4. Depth of field, fog and water: the post passes read the strand depth written by the G-buffer variant; check halos.
Gate: edge-flicker metric on hair pixels at most the cards' in one run; no energy change; no DoF halo in PNG pairs.

### M4. Indirect light on strands. Stream A. Offline, then live.
1. Diffuse indirect (GI) through the dual-scattering multiple-scattering term instead of a Lambert response (C4).
2. Specular indirect: the R lobe of the fibre BSDF from the RT reflections (or the environment) on strand pixels.
3. Many local lights: the fibre BSDF in D-LS's light sampling (ReSTIR-style reuse) instead of only the lights with a
   traced shadow.
4. Radiance cache: SHaRC entries on strands with the fibre albedo, for stable indirect light inside the volume.
Gate: white-fibre furnace within 3%; key-on vs key-off energy on a static camera; one live A/B per item.

### M5. Every view. Stream A. Offline, then live (C6).
Portraits, dialogue close-ups and other small passes; instanced hair (FS 0ca5ee93); strands in reflections and
water hits shaded with the fibre BSDF. Gate: per-pass strand participation lines; no card-only pass left in smoke_ultra.

### M6. Performance. Stream C. Offline, then live.
Shader Execution Reordering on the strand ray queries (divergent), half-resolution GI/AO traces for strands far away,
the crowd budget tuned from the live cost lines. Gate: the cost table per character (GPU ms per pass, VRAM) within
the user's budget on Ultra; no quality change (same-run A/B).

### M7. Content. Stream B. Offline (needs M1).
Beards, brows, fur and hat hair through M1 (the short-beard class passes with the fibre reference on the sample).
Character-creation dyes: check that the strand colour follows a dye change (material at the strand's UV) on a created
character. Gate: per-category pass counts; a dye A/B in one live run.

### M8. Qualification (C7). Stream C. Live.
Ultra preset entries for the qualified keys, GUI pages (Advanced/Experimental), docs with what to look at and which
save, smoke_ultra clean, cost within budget.

### Out of scope
Strand dynamics of their own (collision, per-strand wind): the strands follow the game's simulated cards. Hair of mods
not converted on the user's PC. The game's ambient model outside RT passes.

## Live batches (one run each, prepared offline, predicted.json first)
| Batch | After | Content |
|---|---|---|
| L1 | bundle12 | hv5 (stages + header order A/B), hair_crowd |
| L2 | M2 + M3 | shadows and image quality A/B in one Dialogue1 + Campsite run |
| L3 | M4 + M5 | indirect light and views A/B |
| L4 | M6 + M7 | cost per character, dyes |
| L5 | M8 | smoke_ultra with every qualified key, the user's check |
