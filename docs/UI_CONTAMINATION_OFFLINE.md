# Offline UI / foliage contamination investigation - 2026-10-30

Implementation update: after the user's "proviamola", the allocation-lifetime
reset was applied via common/descriptor_epoch.h and the layer rebuilt for a user
trial. Build/CTest 16/16 and all nine cases using the fixed production allocation
function pass. No live quality result or main merge. The material-owner and RR
issues below remain unchanged. Details: top of PROGRESS_CODEX.md. The original
unapplied patch/source harnesses below are retained as historical evidence.


Analyzed main 47ac6a9, the two user screenshots, a snapshot of the user's current
15:13 session log, native shader sources, and complete frames 43186 / 44920 /
45580 (captures 20261030-152728, -152758, -152811).
No game launch, queue run, focus change, setting change, shader edit, DLL rebuild,
process-memory inspection, merge, or live fix was performed. The user's game was left alone.
This directory contains analysis outputs only, not a deployed correction.

## Result and confidence

A real descriptor-lifetime bug is reproduced in the current production CPU code.
It is the first concrete contamination route to fix. It can expose old UI resources
to RT after handle reuse, without the UI being drawn directly into the RT color image.
The exact blue frame has not been matched to its descriptor epoch/profile bytes;
therefore this is NOT proof that one bug explains every screenshot/flicker symptom.
The prior tests with zero matched near-UI foliage support do not rule the symptom out.

## 1. Proven: recycled descriptor handles retain the old set's contents

layer/tracker.cpp:422 AllocateDescriptorSets records only the new set layout.
It does not clear tracker().sets or dynamic_counts for the returned handle.
The tracker also has no FreeDescriptorSets / ResetDescriptorPool /
DestroyDescriptorPool bookkeeping hooks. track_set_write merges individual bindings
with insert_or_assign; it does not replace the whole set map.

When a driver recycles handle H:
1. Old set H contains, for example, an interface image and a small parameter buffer.
2. The game retires H. The tracker keeps its old map.
3. A new allocation returns H with a new layout. The tracker changes the layout only.
4. The game correctly writes the bindings its new shader needs. Other old map entries remain.
5. Our RT optional queries see those old entries, although they are not valid inputs
   of the new shader. The native shader need not read them, so native rendering can remain correct.

Reproduction uses the UNCHANGED production bodies of decode_write, merge,
track_set_write and AllocateDescriptorSets. Only the Vulkan driver allocation is
stubbed; handles, descriptor records and tracker mutations execute in native C++.
The driver stub returns a recycled handle; it does not access the game or GPU.

Current code result:
  recycled layout=8; stale profile=1; stale UI atlas=1; stale dynamic count=1
  optional profile still sees old buffer 900, range 144 bytes; exit 1
Candidate allocation-lifetime clearing:
  stale profile=0; stale UI atlas=0; stale dynamic count=0; exit 0

Files: descriptor_recycle_current.cpp / .exe, descriptor_recycle_candidate.cpp / .exe.
production-functions.sha256 fingerprints the extracted current function bodies.
allocation-lifetime-candidate.patch is the minimal un-applied patch (not a full
UI-quality-qualified fix). No production source or live binary was changed.

Extended source regression: 9 cases. Current production: 4 failures; allocation
clearing candidate: 0 failures. Includes same-layout reuse, batch and failed
allocation, preservation of unrelated live sets and ordinary partial writes.
Versioned source harnesses: docs/evidence/ui-descriptor-20261030/{production,candidate}-repro.cpp.
Compile with MSVC /EHsc /std:c++17 into captures/offline-ui-20261030; expected exits
are 1 (current bug reproduced) and 0 (candidate). The minimal patch is UNAPPLIED.

## 2. Proven code path: stale optional material inputs can reach foliage shading

core/gi.cpp:398 snapshot queries s1b4 (RT3) and s1b28 (SSS profiles) regardless
of whether the active native shader consumes them. The profile check is only
buffer != null and range > 0. RT3 checks width, not full format/height identity.
core/gi.cpp:2105 keeps the first nonempty x_prof/x_rt3: later native material
permutations do not replace an already nonempty but semantically wrong input.
query_binding reads the tracked descriptor map, not the active shader's declared inputs.

Native GLSL contracts independently establish the distinction:

| Native lighting permutation | s1b4 RT3 declared | s1b28 SSS profiles declared |
| --- | --- | --- |
| f469db78 standard | no | no |
| 4265b844 anisotropic | yes | no |
| 118833ff skin | yes | yes |
| 19763259 translucent / leaves | yes | yes |
| 34a007cc mixed | yes | yes |
| 3033c895 hair | yes | no |
| 95af6e4f cloth | yes | no |

Sources: captures/20261026-173007/shaders/<full hash>.glsl, inventoried in analysis.json.
This is why merely finding a live buffer at b28 does not establish SSS ownership.
Clearing allocation epochs prevents the reproduced reuse case, but optional reads
also need the active material contract: legitimately reused live sets can retain
bindings unused by the current shader too. Do not substitute a size heuristic.

shaders/rt/bg3_materials.hlsli:54 reads 64-byte profiles for classes 3/4, taking RGB
tint, scattering coefficients and thickness from these bytes. bg3_sss_T at line 120
feeds that tint and an exponential into transmission. A wrong finite buffer passes
bounds/NaN guards and can create very large, colored light on thin materials.
RT3 also controls class-specific foliage/cloth parameters. This route is consistent
with foliage/flags being affected, and works with RR off because the RT passes
modify diffuse/specular BEFORE the SR/RR choice. This is a mechanism, not yet a
frame-specific visual attribution. Do not fix it by clamping brightness or disabling transmission.

Real session clue: the retained log labels b28 ranges 16384, 144 and 327680 bytes as
"SSS profile list" (15:14:03 to 15:15:56). 144 is not a multiple of the documented
64-byte profile stride; the existing byte clamp can even choose offset 80 in that
range. The changes are suspicious, but range alone cannot prove which buffer was
wrong or correlate it with a UI event. Logging stops after eight size changes.
The log does not retain descriptor-set allocation epochs or profile identities.

A second compiled reproduction executes the UNCHANGED optional-profile update block
from core/gi.cpp:2113. A stale-but-live first candidate (buffer 900 / range 144)
is followed by a real material source (buffer 901 / range 16384). Current code
retains 900 and makes ZERO queries of the real material source; exit 1.
Source: docs/evidence/ui-descriptor-20261030/material-source-repro.cpp.
This independently confirms the first-candidate freeze. It does not show that
buffer 900 or its synthetic numbers occurred in the user's blue frame.


## 3. Independent RR instability: a normal exposure helper is treated as an unknown effect

layer/rrpost.cpp kCsAdapt recognizes bd9db92d9abc7fb7; check_unknown rejects any
other unlisted CS inside the post chain. The user's log records four refusals for
4681b95ef9f2e54d and four "bloom composite out of order" refusals before its eight-line cap.
Native source captures/alpha_cache/4681b95ef9f2e54d.glsl shows a 64x64 luminance
reduction writing exposure coefficients; it is not a UI compositing shader.
The color chain is thus rejected for a helper that does not itself rewrite scene color.
layer/ngx.cpp switches between pre-post RR and RR on graded color, with a history
reset when mode changes. Such switching can add RR temporal instability. Its frequency
and correlation with the blue frame are not measurable from the capped messages.
This cannot be the only cause, because the user also reports the artifact with RR off.
A correction must recognize the helper's role while preserving exposure ordering;
blindly adding hashes to the allowlist does not address every ordering failure.

## What the retained captures establish, and what they do not

All three new full traces have zero reported non-finite components. Their main
G-buffer stays at 2560x1067, and each contains the expected native lighting sequence.
The stage inventory in analysis.json identifies albedo, post-lighting diffuse,
scene composition and 3840x1600 pre-UI tonemap separately. The final UI composite
046c6949 is downstream. Prior inn captures located floating text in UI, absent
from hudless/RR input; that does not qualify every UI type in this session.

The three retained images do not reproduce the large saturated blue patches from
the supplied screenshots in the inspected scene-composition views. They are NOT a
same-view UI-off/on ablation and cannot settle the exact first corrupt stage.
The descriptor image inventory omits buffer identities and is truncated per set;
an absent res key does not prove that a binding was absent. No shader-buffer payload
was saved. A literal font texture in these three RT inputs is NOT established.

## Next correction and deciding evidence (prepared; not run)

1. Reset descriptor contents/fallback dynamic counts on every successful allocation
   of a set handle. Track retire/reset/pool lifetimes if queries can outlive retirement.
2. Obtain optional RT3/profiles from a native material permutation that actually
   declares them and shades the same output/G-buffer. Reject stale provenance;
   do not accept the first nonempty unrelated binding or invent a range threshold.
3. Offline regression: recycled UI-to-world handles (buffer and image), non-recycled
   partial writes, batch allocation, failed allocation, same-layout handle reuse,
   and material-owner handoff with a stale-but-live first candidate.
4. Only later, in a qualified live check, retain set allocation epoch, source CS,
   buffer/view ID, offset/range and RT3 identity for the RT snapshot AND the native
   class-4 dispatch in the SAME visibly corrupted frame. Capture diffuse/specular
   before RR, RR/SR output and actual rendered/generated output with UI transition.
   This discriminates pre-RR material corruption from FG-only UI reconstruction.

No quality pass or flicker-resolution claim. Existing fixes remain untouched.
