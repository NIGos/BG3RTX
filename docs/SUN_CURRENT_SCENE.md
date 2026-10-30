# Current-caster sun scene: implementation and remaining gates

`rt_sun_cascade_current=0` is default off. Requires cascade capture and ABI 19;
both host and core must be rebuilt together. No game/GPU execution or deployment.

## Implemented recording path

Exploration trace 163004 has camera geometry at submit 1, command buffers 5/7,
four native cascades at 16-19, and lighting at 23. GPU order permits a current
cascade build before lighting, although CPU worker recording order differs.
Presentation tags are not used as native epochs.

The host retains the camera scene's instance input, excluding its old cascade.
A private per-slot command buffer GPU-copies it, preserving native transforms
patched at submission, then builds a separate current-cascade BLAS and sun-only
TLAS. The camera TLAS and mutable global draw collection are not rewritten.
The owning SceneSlot retains original mesh/material references until retirement.

Native Frame set 0 binding 0 is snapshotted as its eight-word packet, including
temporal dither phase. Camera, cascade and lighting packets must agree; projection
bytes are revalidated at submission. Producer ownership requires exact command
reset generations, not merely reused handles or recorded frame numbers.

Private work is inserted before lighting only when BOTH producer generations
occur earlier in the SAME actual VkSubmitInfo entry. No extra QueueSubmit is
issued. Device-group/protected or unsupported ordering leaves the native submit
unchanged. Submit2 signals on modified entries use ALL_COMMANDS; values and other
semaphore fields remain unchanged.

The entire original scene stamp is GPU-copied, preserving Hair byte 12 and camera
bytes 256+. Sun fields: byte 16 conversion-invalid count, 20 ready generation,
24 proof flags, 28 affine-offset error bound, 32-79 clip rows, 80-111 native Frame
packet. Initial CPU words are zero. A rejected insertion therefore cannot authorize
traversal of its unbuilt TLAS: the capable shader returns UNKNOWN/native fallback.
Other RT effects keep their original TLAS/alpha pair. The tuple carries its alpha
layout explicitly, so later material dispatch can extend only this private layout.

A GPU pass rejects streams containing nonfinite or collapsed/unrepresentable
vertices. Geometric trust rises only when the ENTIRE requested ray fits the
verified affine volume. No ray is shortened to the native light-near plane.
Current queries remove legacy 0.5 m / 2%-of-distance cascade hit suppression;
those ignored intervals cannot certify full physical visibility.

## Limitations: no world-shadow gate pass yet

- The first material witness is whole-cascade and strict. Conditional discard,
  fragment-depth replacement, unknown state, skipped cutouts, geometry/capacity
  failures or unproven duplicate omissions withhold it. Legacy alpha filtering stays intact;
  unsupported cards are not newly made opaque in the baseline TLAS.
- Foliage-heavy exploration can still have zero accepted tuples. Per-native-draw
  geometry/material dispatch and conservative per-ray UNKNOWN are required to
  expand coverage while exact recipes are implemented. A shader with unbounded
  depth displacement cannot be certified by a flat-card miss.
- Existing camera draw-key tables include uncaptured draws. Future dedupe must
  prove successful affine camera-mesh inclusion in the exact native epoch; no
  distance bias may conceal coarse shadow-LOD self-intersections.
- This first implementation tests the generic rt_tmax (300 m). Cascade 2's
  observed depth is only 168.2 m, so complete materials alone can still fail it.
  That generic cap is NOT the user's requirement for an infinite physical world.
  The next step derives the finite native SUN domain from actual receiver/CSM
  selection and all relevant current cascade layers. A source-side boundary is
  legitimate only after that coverage proof, not from arbitrary cascade 2 clipping.
- OMM-active sun tuples also need matching non-OMM references and optional content
  descriptors. `reference_tlas` is currently null; combined traversal is not gated.
- No new GPU cost/visual result is claimed. Existing core RT scope logs exclude
  this new private BLAS/TLAS build; its actual cost remains to be measured.

## Checks and later user-coordinated gate

Offline: both ABI-19 DLLs rebuild, full CTest, native epoch/completeness/command
generation/submission tests, finite affine-ray oracle, and ordinary shader
byte identity against 92183e7. These do not replace GPU validation or same-view A/B.

Use exploration bridge/camp-tree views, not just close-ups. Require recorded AND
same-submit inserted current tuples, nonzero certified pixels, zero invalid
accesses and actual RT. Zero records or zero full-ray coverage is INVALID.
Then compare native/RT/final masks, penumbra growth with caster separation, temporal
stability and static expected energy in the same view. Do not tune radius, bias
or brightness to reduce the residual. No live job was launched or queued here.

## Native clipping witness (offline follow-up)

The private path now records actual pipeline raster state before interpreting
its cascade projection as a finite domain. Missing raster state, enabled depth
clamp, disabled depth clipping, negative-one-to-one clipping, or any unobserved
dynamic override withhold the certificate. This does not change the ordinary
path. The CPU regression exercises extension and dynamic-state interactions
without Vulkan calls. Both DLLs and all 25 CTests pass; no live gate or GPU cost
has been measured.

The native receiver audit additionally establishes spatial feather selection of
the first supported CSM layer and optional next-layer blending, not distance
splits. Auxiliary shadow arrays can override or restrict the four-layer result.
Capturing all four actual draw domains plus the enabled auxiliary state and
per-draw opacity remains necessary; a single far cascade is not a world gate.

## Four-layer capture preparation

With the experimental key on, all four actual CSM draw passes now have separate
three-slot XFB rings. The configured legacy cascade keeps its original ring and
metadata. Additional rings are allocated lazily; the ordinary key-off path is
unchanged. Exact native epochs, producer generations, per-layer camera and clip
state are retained independently. A four-layer snapshot is returned only when
all layers match the epoch and the same actual depth-array image. No far-layer
containment or equal shader-set assumption replaces draw membership.

This prepares the native finite-domain consumer; the current query still uses
the original single selected cascade and strict segment proof. Auxiliary domains,
per-draw opacity and receiver selection remain pending. At the default 64 MiB
capture capacity, the nine complementary ring buffers can add up to 576 MiB
when allocated. No live allocation, GPU timing or visual gate was run.

## Exact native receiver source packets

The default-off native Sun draw hook now retains its actual source bindings:
Frame 32 bytes, camera 224, Sun UBO 544, auxiliary UBO 144, and the 48-byte VS ray
basis. It records CSM and auxiliary image/view identity plus the exact native
command generation. Bound auxiliary images never imply the auxiliary branch is
enabled; that decision comes from the captured UBO values. CPU-mapped bytes are
snapshotted where available. An unmapped UBO remains a valid referenced GPU
source, with no fabricated CPU values; GPU copying and validation before ready
publication is still required for that case. No new consumer trust is enabled by
these packets yet. Both DLLs and 25 CTests pass; no live test was run.

## Camera/Shadow membership separation

The interim cross-pass source-identity preparation is superseded and removed.
Current Sun rays use only the actual native Shadow caster membership (mask2).
Camera instances are retained only for primary checks/debug (mask1); their
nonzero masks are narrowed by GPU compute after the source instance copy and
late native transform fixups, before private TLAS construction. Disabled invalid
camera transforms stay disabled. This avoids duplicate camera/Shadow geometry
and the need to assume source-buffer equality proves equal contents between
native passes. No heuristic duplicate filtering or extra geometric-equivalence
framework is required.

## Raw current cards and isolated legacy compaction

The current four-layer stream now retains every capturable native card, including
cutouts omitted by the legacy opaque path. Unknown material/depth behavior still
withholds the current certificate; these cards are never called opaque by
assumption. Each current layer has its own raw ring. For the configured legacy
cascade, a separate GPU copy compacts only ranges admitted by the existing
legacy alpha/dedupe policy, after world conversion. The legacy F-1 TLAS therefore
never receives newly captured unknown cards as solid geometry. Its destination
buffers explicitly include TRANSFER_DST, including key-on transitions from
previously allocated ordinary rings. The actual legacy-policy helper has CPU
regression checks for opaque, texture/scalar discard, mode0/1/2 and duplicates.

The fourth private raw ring means the maximum incremental capture capacity is
now 768 MiB at the default 64 MiB per buffer (twelve complementary buffers),
in addition to the unchanged 192 MiB legacy ring capacity. Actual allocation,
GPU copy/build time and quality remain unmeasured. Both DLLs and 25 CTests pass.
Per-draw opacity dispatch is the next consumer; unsupported material/depth paths
still prevent this preparatory capture increment from being a world gate pass.

## Canonical material snapshots

Stable native Hair coverage/procedural commits are integrated. Canonical alpha
capture is now factored from alpha_add_draw without touching camera record or
texture-index tables. The ordinary camera path retains its raw-binding/indexing
behavior. Current native cascade draw records can independently retain kind1/2
texture cutouts, kind3 scalar coverage, and validated stride88 kind4 native Hair
shadow coverage, with both actual views/layouts/image identities. Unsupported
fragment-depth or dynamic depth state stays unknown. Private descriptor-local
remapping and per-candidate material evaluation are still pending, so mere
successful snapshots do not authorize trust or silently make unknown cards solid.

Both DLLs and 27 CTests pass. Against the stable native Hair baseline4ca35f5,
214 existing shader entries are byte-identical and all10 new entries compile
and validate. This is offline integration evidence, not a live world-shadow gate.

## Per-candidate material dispatcher (implemented, still default off)

The private descriptor tuple now GPU-copies the original camera record prefix
and appends independently indexed native Sun material records. Raw source views
are checked against their captured image identities, assigned private texture
slots, and retained separately. Native cutouts, scalar coverage and validated
native Hair Shadow coverage run their captured formulas at each candidate.
Unsupported bounded cards or invalid coverage reads produce UNKNOWN and preserve
the original native mask for that ray. Fragment-depth geometry that cannot be
bounded by the captured card still rejects the scene certificate.

Camera instance masks are narrowed to1 by GPU compute after late native fixups
and the instance copy, before private TLAS construction. Sun visibility sees only
native Shadow mask2; primary checks retain camera mask1. This removes camera/CSM
duplicate acne without a near-hit bias or speculative source-identity dedupe.
All current wrappers explicitly enable the cascade-ID path even when the baseline
scene has no legacy cascade. Private copy/update overlap has an explicit transfer
write dependency; the source Hair-invalid byte12 remains preserved.

Offline gate: both DLLs build, 27/27 CTests pass, 49,443 coverage/dispatch-range
checks, and214 existing shader entries byte-identical against Hair4ca35f5.
All10 new entries compile and validate. The unchanged ordinary shader paths are
therefore preserved. Native selected/next-layer and enabled auxiliary-domain
consumption is still pending; this increment is NOT a world-shadow gate pass.
No GPU or game execution, visual gate, frame-time measurement or deployment was
performed. The additional private record copies also have unmeasured GPU cost.


## All-four native-domain consumer checkpoint (offline)

`rt_sun_cascade_current=0` remains the default. The private scene now retains all
four native Shadow memberships, with masks 2, 4, 8 and 16. Camera instances keep
mask 1 for primary checks. Native receiver selection and next-layer blending use
the audited spatial feather formula, not guessed camera-distance splits. The Sun
ray ends at the verified native caster-domain front. This is the finite engine
Sun visibility contract; it does not assert that 300 m of uncaptured world is clear.
The entire angular disc must remain inside captured support, independently of
which random direction is sampled. Candidate hits outside the native clip volume
are skipped; numerically ambiguous boundaries remain UNKNOWN.

A same-submit GPU gather snapshots the actual Sun, auxiliary, camera, Frame,
ray-basis and four caster-camera buffers. Native receiver and lighting camera,
Frame and ray packets must agree bit for bit. Actual caster projection plus full
native viewport orientation derives the caster-W to lighting-P transform. A
shared numeric-error-bounded metric test rejects scale/shear or unsupported
projection instead of inventing an origin selector. Raster depth bias stays out
of the physical geometry. Full native viewport/scissor, ordinary depth clipping,
reverse-Z empty-depth clear and current ownership are required. Enabled auxiliary
sampling remains UNKNOWN only where its actual branch affects the receiver.

Legacy alpha-table membership is not a completeness certificate. Plain cutouts
are accepted only for the two audited native predicates with matching captured
UV/channel/scalar values. Native Scalp and Hair retain their explicit formulas;
other bounded candidates fall back to the native mask. Unsupported fragment-depth
geometry rejects scene readiness. The private record copy preserves all 208 bytes,
including the reserved Pubes subtype at byte 196. No blanket trust override or
radius/bias increase was added.

Gather descriptors require actual UNIFORM buffer usage, full created ranges and
aligned offsets, 12 per-stage uniform descriptors and 13 push descriptors. Private
Sun binding 10 references its own TLAS; binding 11 has a valid dummy until combined
OMM integration supplies its actual epoch buffer. The copied OMM proof bytes
128..159 are cleared, preserving Hair-invalid byte 12 and camera bytes 256 onward.

The new counters distinguish certified pixels, fallback, packet failures, active
auxiliary coverage, cone escape, unknown candidates, actual selected/next ray
samples and native-white pixels without any ray. Native-white or missing RT is
never an RT gate pass. Both DLLs and all 28 CTests pass; final shader identity and
validation are recorded in the checkpoint commit evidence. GPU execution, visual
energy/penumbra and primary-match gates are explicitly missing. No game launch,
deployment, new GPU cost or quality pass is claimed. Extra capture capacity is
up to 768 MiB plus private records and build resources; actual residency and GPU
milliseconds must be measured in the coordinated live check.


Raster limitation for the coordinated gate: physical RT still treats captured
triangles as two-sided, like the baseline. Per-draw native face culling is not
replayed by this checkpoint. The current raster witness proves finite clipping
and full coverage, not exact native raster visibility equivalence. This must not
be reported as a completed native-equivalence or physical-quality live gate.


Final offline checkpoint: both DLLs rebuilt successfully; 28/28 CTests pass,
including 271,677 native Sun shared-math checks and 49,443 caster coverage/dispatch
checks. Against Scalp checkpoint `fafc442`, all 214 ordinary shader entries are
byte-identical and all 11 new entries compile and pass SPIR-V validation.
Evidence: `captures/sun-coverage-20261030/build-final-checkpoint.txt`,
`captures/sun-coverage-20261030/shader-final.txt` and
`captures/sun-coverage-20261030/shader-check/result.json` in the Sun worktree.
GUI contract: one startup/restart control `rt_sun_cascade_current`; requires
`rt_cascade_casters=1`, `rt_bvh_split=1` and active RT. Keep it off in every preset
until coordinated GPU, energy, moving primary-match and visible penumbra gates pass.
