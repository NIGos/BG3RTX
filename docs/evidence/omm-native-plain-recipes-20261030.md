# Native plain-alpha eligibility audit

Read-only source/log audit. No game, GPU work, renderer/settings changes or saves.
Sources: current alpha_fs.txt, retained runtime SPIR-V and GLSL, packaged asset
database SHDR/SREF/MVAR/MTPL provenance, and three world F10 captures 162008,
163004 and 163010 on 2026-10-30. This is bounded coverage, not all game shaders.

120 tex recipes exist in the table. Eight occur in these world captures. Only
two have an exact direct sampled-component hard-cut predicate:

| Native FS | Observed VS | Predicate | Texture | UV | Native sampler | Geometry caveat |
|---|---|---|---|---|---|---|
| 16ac1391689391d0 | f4b35620d54abc6c | sampled A >= 0.5f | set1 binding1 | VS input4 -> output0 -> FS input0, no extra scale | Instance transform, no deformation in this VS |
| 42d7e2cf1c9a7e24 | 910cffb017c2fb37 | sampled A >= 0.2f | set1 binding3 | VS input4 -> output0 -> FS input0, no extra scale | Wind/time/texture-dependent vertex deformation |

Both use sampler set0 binding7. Neither FS reads another coverage input, writes
gl_FragDepth, changes UV, applies noise/fade/material factors or has color outputs.
Fresh SDK decompilation of both FS and VS modules matches the retained GLSL bytes.
The table stores exact float cutoffs and texture/channel/UV provenance; it accepts
only kind2 records with matching first/effective cut and unit UV scale. Unknown
FS/VS pairs, VT kind1 and native scalp kind3 decline.

These are SHADER PREDICATE recipes, not complete runtime certificates. The factory
still must prove captured post-VS geometry/UV identity, no extra tessellation or
geometry stage, the same sampled view, full reachable mip support, content epochs,
and the sampler's actual domain. F10/DescEntry retains the sampler binding but not
its addressing/filter/border state. In particular, do not assume REPEAT instead
of a potentially different native border/address policy. Current post-VS geometry
is mandatory for the wind VS; immutable static-geometry assumptions cannot include
it merely because the FS is simple. The shared 16ac FS maps to many packaged
templates including Base_AlphaTest and UI/Base: hash identity does not identify
the actual object's material or establish that it belongs in the caster domain.

Six existing tex recipes are excluded rather than reproducing an incomplete
baseline rule:

| FS | Packaged family | Native factor missing from raw-alpha recipe |
|---|---|---|
| 9aaa6d61f7d2d9a9 | Helper/Helper_Invisible, StaticDepth/Shadow | Two texture alphas multiplied by material scalar |
| a912cbe3dc896c46 | Helper/Helper_Invisible, instanced | Same two-texture/material product |
| e752d4330d455339 | Common Base/environment/foliage depth | Conditional camera-cutaway/noise fade factor |
| 03a2d9f645625c44 | CHAR_Scalp shadow | smoothstep(A) times B/root material transform |
| c22b20253e25e473 | CHAR_Fur_IDmsk camera | Frame/FragCoord dither, B smoothstep and view/normal coverage |
| 1e144b6e40e48558 | CHAR_Fur_IDmsk shadow | B smoothstep, view/normal coverage and material multiplier |

Source confirms missing factors, not their live values or a specific visual
artifact. A native factor cannot be assumed to equal one. Fix/capture the actual
coverage first before adding these shaders to an OMM eligibility set.

Observed counts: 27 calls match the two simple FS predicates across the three
F10s: one camera-depth call and 26 D16 calls. The other tex recipes account for
18 calls. F10 n is a draw-call count, not primitive count, and repeated cascades
are not unique geometry. No F10 triangle total is inferred. Existing independent
queue log p1-fg-static-effects.1.145054.log has 23 reported frames with the 16ac
camera candidate at one draw / 98 triangles. Existing user log reports 158 frames
containing that hash, 1-14 draws / 90-6,340 triangles depending on view (largest
example 11 draws / 6,340 triangles at 16:31:16.190). These are per-frame top-12
log samples, not unique meshes, exact F10 alignment or a speedup forecast.
No triangle report for 42d7 was found in those bounded log sources.

The small common/omm_native_plain_recipes.h table has NO production caller.
Its 34 CPU checks pin pair lookup, exact record matching and negative cases.
For stochastic thresholds [0,1], uniform decoded one may be Opaque only after
the full proof; alpha zero stays UNKNOWN because native >= accepts hash zero.
All provenance, hashes, package references and observed counts are in the sibling
JSON. Do not present this narrow whitelist as completed foliage/native coverage.
