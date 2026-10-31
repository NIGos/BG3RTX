# Order-independent native-hit packets — 2026-10-31

Fixes the actual acquisition failure of frozen `177ef6e`. R1 repeatedly recorded
the deferred pass before source preparation: frame 8760 deferred at 04:26:28.109,
source at .113. RT was active 120/120 with primary match 99.525% and healthy FG2,
but material GPU attempts were zero. This was **not a material/provider pass**.
Current-frame TLAS selection was observed throughout; no future-TLAS selector
or alternative camera/frame offset is introduced.

The host/core ABI is now **21**. The typed material-use callback validates the
original selected TLAS, baseline AlphaDSL and build identity, then returns a
query-owned AlphaDSL through an explicit output. Core binds this returned set
only for that material dispatch. Existing OMM selection/reference semantics and
ordinary/private-Sun consumers remain unchanged. Matching host/core/launcher
builds are required. `rt_hit_material` remains default OFF.

Deferred draws capture independently into bounded per-recording packets. Each
owns its post-VS clip/UV/native basis, actual execution-time material snapshots
and cloned native samplers. Source scene preparation no longer needs to occur
first. In the observed deferred-first case, source copies are filtered by the
already captured **unique exact draw keys**; unrelated generic VT records cannot
consume the 16 MiB source-copy budget. Source-first is also supported within the
same bounds. The CPU join maps local deferred snapshot IDs to actual source
AlphaRec IDs; duplicate/unsupported/ambiguous candidates remain unknown.

Once both packets exist, each query records a private compute assembly into
its own basis/rows. It compares deferred execution-time parameters against source
alpha hints, copies both vertex streams, and retains the exact clip/UV proof.
No query storage aliases source/deferred storage or another query. A clone of
the exact existing main AlphaDSL preserves baseline records/textures/immutable
samplers and optional OMM bindings 10/11; only material 12–15 are private.

Each query owns a zero-initialized host-coherent publication word, outside every
GPU Fill/Update range. Immediately before the actual driver submission, exact
recording IDs and actual queue order must prove both producers precede the
query on the graphics queue. Successfully submitted earlier producers count;
merely pending producers do not. Same-CB order is accepted only when preparation
or snapshot commands were already recorded before query registration. The word
is immutable after the first successful submission. Failure clears tentative
proof, so a later invalid retry cannot inherit a prior 1. Query assembly and
consumer entry both check it before reading producer data or tracing rays.

Exact producer/deferred/query owners and submit reservations retain resources.
Known reset/free and actual GPU completion retire them; no frame-age fallback.
Descriptor pools, private buffers and packet samplers retire only after their
actual owners. Fresh startup OFF performs no added GPU allocation/work; runtime
OFF creates no new packet/source copies while held owners still retire safely.

Existing fixed bounds remain: 16 accepted draws and 32 MiB deferred basis per
packet; 16 MiB source copy in its existing 32 MiB slot buffer; 32 MiB maximum
combined basis and about 2 MiB rows per query. There are at most eight deferred
packets and sixteen query packets, allocated lazily. Worst-case storage is
about 1 GiB including eight existing source buffers; actual query basis
allocation is only the mapped bytes. No user capture budgets are resized.
CPU descriptor-copy cost, GPU milliseconds and actual supported pixel coverage
remain unqualified until the next acquisition probe.

Query entry/rejection logs now expose actual recording level/flags, frame,
selected build identity, camera bindings, packet readiness and mapping counts.
Deferred logs distinguish static seen, pipeline/binding declines and snapshots.
Submit logs state the actual ordered publication result. These reuse existing
rt_stats throttling; zero material attempts still cannot be a quality pass.

Validation: matching ABI 21 host/core/launcher build; 10/10 targeted CTests;
131 submit-order/publication cases, both CPU recording orders, local/source ID
mapping, late duplicates and distinct typed query-set output. All 34 involved
material/direct/gather/join shader modules compile and pass Vulkan 1.3
spirv-val. Four representative unchanged ordinary modules recompile to exact
prior hashes. Independent scoped packet/lifetime/descriptor review is clear.
Exact compiler hashes are in the adjacent JSON. No game/GPU execution occurred
in this worker; the frozen R1 candidate was not edited.

Next: one coordinator-owned R2 acquisition smoke, using query logs and actual
field residuals/denominators. Normal RF and its probe replacement remain intact.
The separate direct-only evaluator is retained; its atlas-flag visibility
correction is a separate coordinator commit, not an acquisition prerequisite.
