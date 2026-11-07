# Hair shadows: game maps versus RT strands

Analysis only, 2026-11-04, branch `claude/hair-shadow-maps`. No live evidence added.
Scope: matched, published grooms with `rt_hair_groom=1` and `rt_hair_groom_primary=1`.
These keys do not enable fibre shading. Unmatched styles, scalp, accessories and failed swaps stay native.
Plan: `docs/RTX_HAIR_COMPLETE_PLAN.md` M2.2 (branch claude/hair-batch-lod). Evidence from the primary/integration docs and code.

## Sources and matrix

G = game shadow map containing cards. C = RT card geometry. S = RT groom strands.
E = environment geometry; it is not a hair card or strand caster. None = no geometric shadow.
The matrix assumes ordinary sun, D-LS enabled and valid rays. Mixed sources can darken the same pixel.

- [S] `shaders/gi/rt_shadow.hlsl:124`, `:198`; `shaders/gi/rt_sun_resolve.hlsl:39`: S plus G until fully trusted replacement; G for unknown rays.
- [L] `shaders/gi/rt_local_shadows.hlsl:674`, `:838`: mapped locals replace G, or use min(S,G) under atlas fallback; unknown keeps G.
- [H] `shaders/gi/rt_local_shadows.hlsl:412`, `:807`: mapped local lights never replace class-5 hair lighting.
  (rt_hair_fibre_mapped, M4.3: the fibre pass replaces them on strand pixels where D-LS trusts the trace.)
- [U] `shaders/gi/rt_local_shadows.hlsl:810`, `:821`: unmapped locals use RT where selected; skipped/unknown lights remain unshadowed.
- [R] `shaders/rt/rt_common.hlsli:1373`, `:1444`: matched procedural cards skipped; groom TLAS supplies shadow hits.
- [F] `shaders/gi/hair_fibre.hlsl:126`, `:149`: optional fibre replacement skips mapped locals; sun uses E visibility and strand counts.

| Caster -> receiver | Sun | Local with game map | Local without game map |
|---|---|---|---|
| Hair -> skin/body | S + G; S alone at full trust [S,R] | S or S + G; G if left native [L,R] | S when traced; none otherwise [U,R] |
| Hair -> hair | S + G; S at full trust; fibre uses S counts [S,F,R] | G, even with fibre enabled [H,F] | S when traced; none otherwise; fibre counts on traced lights [U,F,R] |
| Hair -> environment | S + G; S alone at full trust [S,R] | S or S + G; G if left native [L,R] | S when traced; none otherwise [U,R] |
| Environment -> hair | RT E + game E mask; fibre uses RT E [S,F] | Game E map [H,F] | RT E when traced; none otherwise [U,F] |

G shadows are rasterised from cards even when the camera G-buffer shows strands.
`docs/RTX_HAIR_PRIMARY.md:47` explicitly leaves shadow cascades unchanged.
`layer/hair_primary.cpp:424` recognises only depth, deferred and velocity fragment shaders.
Portraits stay native (`layer/hair_primary.cpp:560`); secondary closest-hit skips cards (`rt_common.hlsli:1791`).
Unreplaced hair still casts RT native ribbons/cards, not S; key-on is not global coverage.

## What can remain visible

On the neck, cheeks, shoulders and ground, broad card silhouettes survive wherever G survives.
Sun replacement off multiplies the game mask by RT visibility; it cannot erase a card shadow.
Replacement on still retains/blends G under roofs, at frustum limits and for unknown queries
(`rt_sun_resolve.hlsl:41`, `core/gi.cpp:3135`).
Thus hair can look like fibres while its shadow looks like ribbons or changes with camera trust.
Mapped local lights can cast ribbons onto hair itself, regardless of `ls_shadowed=1`.
On skin/ground, `ls_shadowed=2/3` can retain the atlas silhouette; mode 0 keeps it entirely.
Fibre sun subtraction requires known visibility and positive mask times clouds (`hair_fibre.hlsl:155`).
Its added sun is independent of that mask (`:161`); a zero mask does not itself block the new term.
Unknown rays keep the native term. Fibre is separately gated after D-LS (`core/gi.cpp:3396`).
Legacy cascade RT soup is another possible C source, not a game-map sample.
Alpha filtering/deduplication normally excludes copies (`layer/geocapture.cpp:1421`, `:1448`).
If admitted, cascade triangles bypass groom-card metadata (`rt_common.hlsli:1402`, `:1422`).
Experimental current-sun retains native draws (`layer/geocapture.cpp:1445`, `:1473`)
and tests card coverage (`rt_common.hlsli:802`, `:1417`); enabling it is no general cure.

## Options, smallest first

1. Suppress matched live-groom card depth writes in every native shadow pass. Recommend this first.
Use a new default-off setting. Preserve XFB capture and camera draws; filter RT cascade copies too.
Identify sun passes with `cascade_layer` (`layer/geocapture.cpp:1200`): depth-only D16 array layers.
Match DepthShadow FS 34951669ed4b3b0f / 8dddc0bc49e56efc (static/instanced), with verified VS.
Match tetrahedron FS 00a9ecbb9c2b4ff8 / a39086306bfc9c03 (`docs/NATIVE_HAIR.md:73`, `:74`).
For local atlases/far caches, verify depth target membership too; the sun-array test is insufficient.
Join the live groom and placement; primary keys omit placement (`layer/hair_primary.cpp:365`).
Invalidate cached shadows and restore cards on groom withdrawal/load; stale atlas depth otherwise survives.
Cost: CPU joins/state changes, no extra rays; GPU ms unmeasured. Risk: stale/missing or wrong-instance shadows.
This removes card ghosts but does not alone provide every missing strand shadow on hair receivers.
2. Replace remaining game terms with RT visibility. Combine with option 1 for card-free fallbacks.
Recover the exact mapped class-5 native term before replacing it; simply deleting the flags check is wrong.
Extend fibre replacement to mapped point/spot and box lights, and cover retained sun/unknown domains.
A mixed min with a card-free map can retain environment blockers, but native-only windows still lack S.
Full S requires published off-screen grooms and valid visibility; otherwise omit hair occlusion consistently.
Cost: more visibility/count rays and term reconstruction; up to eight groom queries per fibre light today.
Risk: leaks, self-intersection, changed energy and unsupported terms. GPU ms unmeasured.
3. Accept/document the matrix: zero added cost, visible ribbon shadows remain. It does not meet full closure.
No existing toggle guarantees all-S or all-none. All-none must omit groom and card casters in every path.

## Option 1 implemented (2026-11-07, branch claude/hair-m5-m2, key `rt_hair_groom_cascade_drop`, off, live)

Smallest safe step: no receiver-side change. In a sun cascade pass (`geo_cascade_layer`: depth-only D16 cascade array
layer) a static hair card draw (DepthShadow FS 34951669ed4b3b0f) whose draw key (index range; the same key as its
camera draws) is in the groom set published this or the previous frame is drawn with a clone of its pipeline
without depth writes. Vertex stage, XFB capture (RT cascade casters) and every other state stay native. Not
touched: instanced hair (8dddc0bc49e56efc), local-light atlases (tetrahedron FS, other passes), unpublished styles.
Attribution: the cascade draw key is the camera key (same index buffer, offset, range, vertex offset), so no new
join is needed; a key shared by two characters of the same style drops both cards while both grooms usually publish
(risk documented above: wrong-instance shadows). Requires rt_hair_groom_primary (the publish).
What it changes per receiver domain: trusted RT pixels use strand visibility already (no change); roof pixels
(native mask x RT) lose the double card x strand shadow; the off-screen trust blend keeps the strand part only;
unknown rays keep the native mask, which now lacks the hair (no hair shadow there). The key trades ribbons and double
shadows for missing hair shadows in the fallback domains: the user decides with the L2 PNG pairs.
Counters: the period line `[hair primary] frame N: ...` ends with `sun cascade hair card draws T, without card depth D`.
Offline proof: CTest `hair_groom_asset_test` test_cascade_card_drop (all four conditions, publish freshness of one
frame). No shader change: 221/221 requests byte-identical to main, request delta 0. Cost: none on the GPU (fewer
depth writes), one hash lookup per cascade draw. Live predicted: D = T for published styles; neck/shoulder card
silhouettes gone under the sun with RT sun replacement; no change on unpublished styles.

## Required live gate, after offline implementation and fresh authorization

Use Test Dialogue1 for matched companion hair, neck/shoulders and self-shadow; add Campsite for ground/crowds.
Confirm these saves contain both mapped and unmapped local lights; otherwise the local gate is incomplete.
Keep primary/groom latched on from launch, after the reload safety fix is qualified. Do not hot-enable them.
In one run compare candidate OFF/ON/OFF; vary `rt_sun_replace`, `ls_shadowed=0/1/3`, and `rt_hair_fibre=0/1` separately.
Exercise roof/frustum fallback, mapped point/spot/box lights, and `rt_sun_cascade_current=0/1` if supported.
Require every ON window to show no card silhouette, correct E-on-hair blocking and no double shadow/acne.
Check static and held Q/E motion, LOD/fallback restoration, then real Ultra; record missing coverage as INVALID.
Use PNG pairs, float pre-post energy and a strand reference; derive tolerances from OFF repeats/encoding.
Log groom/sun/local diagnostics briefly; measure scene build, RT and D-LS GPU ms without them or compiles.
Fibre is inside D-LS timing; compare environment-only regions and unmatched styles for unintended changes.
Lint patterns/expect_every and restoration before queueing. No job was prepared, queued or run for this analysis.
