# Native Hair vegetation capacity regression

Scope: offline source audit of trial `a9f8688` against baseline `92183e7`.
No active trial files/new captures were read or changed; no game or GPU ran.
The rendering coordinator supplied the live log evidence below. OMM and current
Sun were off in that trial; their unfinished paths are not its explanation.

## Confirmed capacity mechanism

Native Hair selects a global 88-byte XFB stride at device creation. Non-hair UV
remains at byte 16; mesh extraction, split metadata and candidate reads use the
encoded stride. The reviewed ordinary paths do not reveal a new hardcoded
24-byte vegetation read. The same physical capture budget nevertheless holds
only 24/88 of its former vertex count. Overflow has visible consequences:
alpha-region overflow becomes opaque coverage, and opaque-region overflow drops
draws from the scene. These can alter vegetation shadows as demand changes.

Coordinator-provided trial log: a 384 MiB buffer had a 230 MiB alpha region,
479.7 MiB alpha demand and 359.4 MiB opaque/fallback demand in one window;
41.57 alpha draws/frame became opaque and 30.41 opaque draws/frame were dropped.
Across all 47 windows, maxima were 660.8 MiB alpha and 634.5 MiB opaque/fallback.
Hair invalid/missing state, unsupported interfaces and retired invalid bounds
were all zero. The global Hair-invalid query guard was therefore not the
observed cause; it remains unchanged.

## Small allocator fix, preserving physical MB caps

The partition feedback incorrectly counted capacity-forced alpha fallbacks as
intrinsic opaque demand on the next frame. Those bytes were already counted in
alpha demand. This double reservation can keep the alpha partition too small
even when the explicit total budget fits both populations.

Commit `2e049e4` changes only startup native-Hair wide capture feedback:
`intrinsicOpaque = max(0, cursor + opaqueOverflowBytes - alphaOverflowBytes)`.
The default 24-byte feedback remains unchanged. Existing alpha partition
arithmetic is extracted unchanged into `common/rt_capture_budget.h` for CPU tests.
Physical allocation limits, 60% maximum fraction, existing reserve factors,
overflow/drop logs, global query guards and shader formulas are unchanged.
The same physical total stream is still reported in diagnostics.

The bootstrap test models A660.85/O634.55 MiB, allowing one half of the printed
0.1 MiB code above the logged maxima. It deliberately treats the reported
opaque/fallback maximum as true opaque demand: a conservative synthetic policy
hazard, not a claim that those two simultaneous true populations occurred live.
From a 64 MiB alpha floor and 1408 MiB budget, old feedback can remain at 64 MiB;
corrected feedback reaches a fitting partition. No larger floor is required by
this fix.

## Explicit trial budget and limits

`rt_xfb_mb` is allocated only when the capture buffer does not exist and is marked
restart-required. Editing it live does not resize the current buffer. No live
resize was added. The coordinator's next trial uses an explicit private-profile
1408 MiB setting and an owned restart, rather than silently redefining a user's
MB cap. This equals `384 * 88 / 24`, exactly restoring total geometric vertex
capacity. It adds 1024 MiB to the one main camera scratch buffer; cascade rings,
mesh-pool residency and allocation/AS costs are additional, separately measured
resources. A device allocation failure must remain a failure.

Under the conservative maxima, the adaptive partition settles near 710 MiB
alpha / 698 MiB opaque, because the opaque 1.1 reserve limits alpha before its
60% cap. Both actual demand bounds fit. This is not full 1.5x alpha headroom:
the full existing alpha/opaque reserve goals would need about 1691 MiB total
with 992 MiB alpha at the rounded upper bounds. No such allocation is enabled.
A 1024 MiB budget cannot cover 660.8 MiB alpha under a 60% cap.

The new CPU regression passes 34 checks: legacy outputs/hysteresis, fixed-cap
semantics, overflow subtraction/saturation, bootstrap recovery, maxima, exact
24/88 capacity equivalence, 256-byte base alignment and uint32/conversion-dispatch
representability. It does not certify the device's available VRAM or limit values.
No HLSL changed; a repeated shader compilation is unnecessary for this host-only
fix. Both-DLL build/full CTest and the actual no-overflow live quality/cost gate
remain the coordinator's integration checks before accepting the candidate.

Keep/reject criterion: after warm-up in the same affected view, alpha-made-opaque
and opaque-dropped counters must remain zero, native records stay nonzero and
their invalid counters stay zero, and vegetation must stop flickering without
excluding its geometry. Capture the existing GPU timing scope and overall fps;
do not infer a new GPU cost from CPU tests. Darkness/dither on Hair is a separate
remaining observation, not hidden by this capacity correction.
