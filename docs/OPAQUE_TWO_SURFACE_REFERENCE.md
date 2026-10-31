# Bounded two-surface opaque reference

Default OFF: `rt_hit_reference=0`. Experimental GUI control **Experimental
lighting reference** selects Off, Direct only (`1`) or Two surfaces (`2`).
Every preset leaves it Off. Native hit materials must be enabled at startup;
the reference selection can then change live. Existing `rf_debug=7` remains a
legacy direct-only view when the new control is Off. No provider, host ABI,
installed personal trial or main gameplay lighting was changed by this step.

This extends the existing `rt_hit_direct` renderer, not a separate renderer/tool.
The direct entry stays byte-identical. A compile-time wrapper selects one extra
BSDF continuation, using the same main AlphaDSL, ABI21 per-query material-use
registration, native material adapter, shared frame, declared FresnelBlend,
point/spot amplitudes and RT visibility. It never binds the private Sun set or
modifies normal RF's probe replacement.

## Supported computation

For a matched raster class0 primary x0 with outgoing wo:

`L_ref = L_pointspot(x0, wo) + [f(x0, wo, wi) * abs(Ns.wi) / pdf(wi)] * L_pointspot(x1, -wi)`.

The first and second direct terms enumerate the actual valid native froxel
point/spot lists, including one physical visibility query for each positive term.
This is deterministic point-light NEE, not area-light sampling or MIS. The
continuation uses the shared `tb_fresnel_sample` mixture PDF and evaluated f;
cosine, PDF and throughput are applied once. Shading/geometric hemisphere
conflicts decline rather than inventing a shading-normal correction.

An NDF null sample is retained as a legitimate zero-continuation event. It is
never retried or normalized away. A supported second material/direct term ends
at exactly two surface vertices; there is no third-bounce or Russian roulette
claim. The existing geometric pixel-cone footprint propagates to the secondary
material; no roughness blur, history, cache or new denoiser is added.

Primary matching uses the established depth/footprint/encoding tolerance and
requires the actual G-buffer class0. Source epoch/publication guards run before
traversal. The continuation is capped by the existing `rt_tmax`; a miss within
that range is **incomplete**, not black sky or proof of an empty world. Invalid
ray parameters, aborted traversal, unsupported secondary material or unavailable
local-light lists are explicit incomplete paths.

## Domain and fallback

Only the current provider's audited class0 opaque VT material subset is eligible.
It excludes held/pool/legacy/procedural and unsupported material recipes. Both
vertices need the actual current provider tuple and native in-frustum froxel
support. A valid empty native point/spot list is a zero for this declared term;
a missing/overflowed/out-of-frustum list is unavailable. Projector-bearing lists
decline. This does not certify world-light completeness outside that domain.

Sun, environment/probes, emission, transmission, additional scattering classes,
MIS, a verified room and full scene closure remain outside this reference.
The two-surface result cannot replace full RF hit radiance or claim full M6.

If the primary material/direct term is unsupported, the prior renderer output
stays. If a continuation is incomplete, the known primary direct prefix stays.
That prefix is NOT a completed two-surface estimate. The mixed fallback image
is not an unbiased two-bounce ground truth, and selecting only completed samples
would also bias an image-wide estimate. Expand the supported domain before any
such claim. Direct and two-surface modes intentionally contain different path
lengths, so equal mean brightness between them is not an acceptance condition.

The kernel has no accumulation/filter. Native RR/TAA/postprocessing may still
filter its displayed output later. Existing pre-RR `pan_capture` diffuse output
can inspect the raw reference; a final screenshot alone is not unfiltered proof.

## Existing statistics and qualification

With `rt_stats=1`, the existing 120-frame readback prints **hit reference
two-surface**. It reports total evaluated paths, supported depth2 completions,
positive indirect completions, BSDF nulls and distinct incomplete reasons:
primary material/direct, sample/domain, traversal/no secondary, secondary
material/direct, numeric/primary mismatch. Counters 269..280 form the path
partition; 281 is the subset with a positive supported indirect contribution.
No dispatch, absent RT, all-UNKNOWN, all-null or zero positive supported two-hit
paths may count as a demonstrated indirect rendering improvement.

Offline gate at source 90d7930: matching core and launcher link; six affected
CTest targets pass. New shared arithmetic has 334 CPU checks, including an
independent analytic f*cos/pdf result, exact point/spot prefix-plus-bounce sum,
coordinate-frame cases, preserved 32/64 NDF nulls, unsupported domains and
nonfinite overflow fallback. Settings pass 292 checks. Sixteen affected existing
direct modules are byte-identical to 90d7930; all 16 new feature variants compile
and pass Vulkan 1.3 SPIR-V validation. Other existing shader/common math inputs
are unchanged. See `evidence/opaque-two-surface-20261031.json` for hashes.

No game/GPU execution, runtime participation, image improvement, noise/energy
qualification or GPU milliseconds are claimed. Acquisition was still failing
bindings in the coordinator's latest earlier smoke, independent of this offline
algorithm. The new pass runs after the ordinary RT pre-pass timer; its cost is
not the existing `rt passes` figure or a historical estimate. Later direct/two
same-run measurements must use the existing whole GI interval consistently and
state its broader scope, with diagnostics off for cost. The reference has no
new isolated GPU timer. The key remains Off until a coordinated supported run.
