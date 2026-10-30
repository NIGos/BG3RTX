# OMM GPU producer - work in progress, 2026-10-30

This branch adds an actual compute producer feeding the production EXT backend.
It is not yet called by BG3's BLAS builder and is not an acceleration/quality gate.
The active goal remains complete OMM, Hair and RT shadows, including live validation.

`record_plain` accepts one registered plain texture view and a triangle vertex
range. Its level-0 micromaps use the exact EXT descriptor order, one primitive per
micromap, four states, four allocated bytes per primitive (only two bits used).
The GPU kernel bounds wrapped neighboring texels over every declared reachable
mip, then compares an outward alpha interval with the native >= threshold domain.
Incomplete domains, nonfinite data or exceeded texel budgets remain UNKNOWN.
The view must resolve through the production image/view/content registry. The
producer derives its mip domain from that view; callers cannot omit coarse mips.
An initial GPU dispatch atomically snapshots image/global epochs and sticky
disable state. Every primitive rechecks them before and after sampling. Output
includes a 256-byte prefix containing that immutable stamp and an atomic invalid
flag. This prepares later scene validation; it does not yet implement stale-scene
reference traversal. Concurrent upload counters are always read atomically.

The default sampling certificate is ZERO. No caller may set it merely because
the code hull, finite fixture, image handle or frame age looks plausible. It
requires matching interpolation/UV/LOD/numeric/content proofs. In particular,
legacy stochastic thresholds cover [0,1]; alpha zero is not universally transparent
because the existing 24-bit hash includes zero. An all-UNKNOWN bake is not a pass.
BC/sRGB source metadata explicitly rejects the general numeric certificate;
only uniform decoded endpoint proof is currently accepted for these formats.
The combined GPU probe preserves the producer/content checks and adds 8,704
BC3/BC7 UNORM/sRGB Load and LINEAR/trilinear endpoint checks: zero failures and
zero validation errors. This does not certify mixed filtered values.

Host ownership retains source snapshot, producer pipeline/layout, micromap input
and descriptor buffers until backend retirement proves completion. Pipeline reload
retains prior recorded generations. If EXT recording fails after compute is already
recorded, one orphan owner is retained until explicit device idle and further
production stops. Actual input allocation sizes are capped at 128 MiB; per-dispatch
declared work is capped at 2^24 texel visits, not an empirically certified GPU cost.
The enclosing injection must restore native compute bindings before returning.

The companion spatial integer-support provider replaces whole-mip scans with a
wrapped UV rectangle and conservative filtering halo. It validates all declared
mips and budgets before accessing source bytes. It is still not a floating-point
GPU sampling certificate.

Validation in the isolated worktree:
- Full build of both ABI-19 DLLs and tools succeeds; 25/25 CTest entries pass.
  Asset DB is absent in this worktree, so its test takes the documented skip path.
- Spatial support: 246,013 independent ideal-sampler/validation checks.
- Producer layout/range/budget planning: 4,137 checks.
- New producer DXC compilation and SPIR-V validation pass. Ordinary shader sources
  are unchanged; this file is not included by any ordinary shader.
- Actual RTX 5090 synthetic producer: production compute creates Opaque,
  Transparent and UNKNOWN-without-certificate outputs, reads back exact wire
  states/status and all three actual GPU content stamps, feeds three real EXT
  micromap builds and retires them through
  the actual production timeline. Zero validation errors. The separate six-ray
  backend fixture and 2,176 endpoint samples also remain green. The six rays
  traverse the backend fixture, not these producer micromaps: do not conflate
  the two into a native-textured scene gate.
- No game launch, live deployment, new GPU pass timing or performance claim.

Required next integration: scene validation of the captured content epochs and
reference selection on changes; material-specific BLAS variants with
baseline/reference TLAS; exact ticket use and
retirement at every scene build/traversal; GPU same-ray known-state comparison;
nonzero useful known coverage and measured BG3 quality/cost with FG active. Higher
subdivision and VT coverage remain open. Level 0 is an initial production wire
format, not the final useful foliage acceleration target.
