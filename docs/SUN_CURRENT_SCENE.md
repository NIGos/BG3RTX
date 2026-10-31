# Current native-cascade Sun candidate

Status: offline candidate, default off; no game/GPU quality or cost gate passed.
Key: rt_sun_cascade_current=0. Requires rt=1, rt_cascade_casters=1 and
rt_bvh_split=1. Experimental GUI control requires restart. Both DLLs must match (ABI 21 today).
Main/user build is unchanged; this source composition is isolated on codex/omm-bake.

## Supported recording and query path

The private Sun scene copies the camera instance prefix before its legacy cascade
instance. Camera instances keep mask 1. Four independent native D16 caster streams
use masks 2/4/8/16 and preserve their native material dispatch. Raw caster capture
is separate from the filtered legacy copy: unknown cutouts do not become opaque
legacy geometry. No approximate equality dedupe certifies current coverage.

The build is inserted before lighting in the SAME actual submit entry only when
all required camera/cascade/receiver producer generations occur earlier there.
The native submit is unchanged on unsupported ordering, device-group or protected
conditions. Exact command reset generations and native Frame packets identify
sources; presentation counters or recycled handles are insufficient. Submit2
signals cover ALL_COMMANDS when appended work requires it, with values preserved.

GPU instance copying preserves native submit-time matrix fixups. Each retained
BvhSunBase snapshots its actual TRANSFER_SRC creation usage. An old source created
without that usage declines before any copy, even after the current ring upgrades.
This is internal host metadata, not a public ABI change.

A same-submit GPU gather reads actual Sun (544 B), auxiliary (144 B), camera
(224 B), Frame (32 B), ray-basis (48 B) and four caster-camera uniform packets.
Exact receiver/lighting Frame, camera and ray data must agree. Native caster
projection and viewport derive caster-W to lighting-P transforms; a derived metric
error bound rejects unsupported scale/shear rather than guessing an origin.
Offsets, created buffer usages/ranges, uniform alignment, descriptor counts and
push-descriptor capacity are checked before recording.

Current Sun visibility rays already use lighting-P geometry. They skip the
baseline-camera origin translation in RT_SCENE_REBASE variants, preventing a
second rebase. Camera-mask primary queries retain their original translation.
This source-review correction still needs actual GPU/native-game qualification.

Receiver selection follows native first-positive cascade, spatial feather and
immediate-next blending. Active unsupported auxiliary coverage remains UNKNOWN.
The WHOLE finite Sun-disc/cone support must fit the native captured volume;
per-candidate clipping enforces native source/cascade membership. Unsupported or
numerically ambiguous boundaries retain native visibility. No generic 300 m ray,
trust lift, larger radius, arbitrary bias or blur is used.

## Materials, private descriptors and fallback

Canonical AlphaRec copies preserve all 208 B, including Pubes subtype at byte 196.
Plain alpha support is restricted to two audited FS/VS/scalar predicates.
Native Scalp/Hair/Pubes retain their specific equations. VT kind 1 and other
unsupported candidate recipes remain UNKNOWN; legacy alpha-table membership alone
is not evidence of native equivalence. Fragment-depth inputs require their own
supported geometry witness.

The private layout adds dispatch at binding 12, four soups at 13/16/17/18 and the
receiver packet at 14. Binding 10 is this Sun scene's own TLAS. Binding 11 is a
valid dummy while OMM renderer integration remains unwired. Copied proof bytes
128..159 are cleared: no micromaps exist in the dedicated Sun scene. Hair-invalid
byte 12 and camera bytes 256 onward survive copying. Missing/unready tuples return
UNKNOWN/native mask rather than opaque camera-caster multiplication.

Counters distinguish certified pixels, native fallback, packet rejection, active
auxiliary coverage, cone escape, unknown candidates, actual selected/next ray
samples and native-white pixels without a ray. Native white or missing RT never
counts as an RT quality pass.

## Evidence and limits

Sun checkpoint 4edd799: both DLLs build; 28/28 CTests, 271,677 shared-math and
49,443 caster-coverage checks pass. Against fafc442, 214 ordinary shader entries
are byte-identical; 11 new entries compile/validate. This is the original isolated
Sun evidence, not the later combined-source gate.

Combined candidate: 36/36 CTests and both DLLs build. After the double-rebase correction,
219 existing entries are byte-identical to 75bb833; 10 new entries compile and
pass SPIR-V validation.
See the current top of PROGRESS_CODEX.md and the combined evidence artifact for
final revision and counts; do not substitute historical checkpoint results.

Maximum extra capture capacity at the default setting is 768 MiB, plus private
records/build resources. Actual residency and GPU milliseconds are unmeasured.
Physical RT currently treats captured triangles as two-sided, as the baseline
does. Per-draw native face culling is not replayed; finite clipping/coverage proof
is not exact native raster-visibility equivalence.

Pending: actual GPU capture/build/packet validation, face/distant-shadow image
quality, energy/penumbra, moving primary match with FG2, and separate build/pass
cost. Unsupported families/domains stay native. OMM useful known coverage,
reference selection and acceleration qualification remain a separate increment.

## Automated qualification boundary

No live grant exists. Future drafts stay outside every watched pending inbox.
Only an explicitly authorized test-queue batch may load Test1/the selected distant
scene. It sets/restores all keys, holds matching poses, captures final/debug views,
performs same-run comparisons and measures RT-active primary match and pass cost.
The user is not asked to edit INI, toggle settings or repeat camera movement.
READY_FOR_LIVE is distinct from a quality pass; features remain off in presets.
