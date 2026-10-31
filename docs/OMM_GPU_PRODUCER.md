# OMM GPU producer - work in progress, 2026-10-30

This branch adds an actual compute producer feeding the production EXT backend.
It is not yet called by BG3's BLAS builder and is not an acceleration/quality gate.
The active goal remains complete OMM, Hair and RT shadows, including live validation.

## Owned post-VS snapshots - 2026-10-30

`allocate_plain_vertices(cmd,bytes,&out)` returns buffer/address/bytes/shared owner
without recording GPU commands. Allocation charges the same actual-memory
128 MiB producer budget, with storage, AS build-input, transfer-destination and
diagnostic transfer-source usage. Zero, unaligned and excessive extents decline.
Pass that owner to `make_native_plain_input`; the existing sampled owner keeps
the private snapshot through ticket retirement, including the retained orphan
path after an EXT failure. The original mesh pool is not captured by this owner.

Set `PlainInput.copy_source`, `copy_source_bytes` and `copy_offset` to request a
same-command source-pool copy. The destination is exactly `push.source_bytes`,
with vertex_offset zero. Aliasing, unaligned/overflowing source ranges and size
disagreement reject before recording. After all producer allocations/validation
and the submit marker succeed, record_plain records source-write to transfer,
the private copy, and transfer to compute/AS-read barriers, then the existing bake.
The caller still owns source validity and TRANSFER_SRC usage through the copy.
Only the private destination must remain alive for later attached BLAS traversal.

Full DLL build and 31/31 CTests pass. Existing RTX 5090 fixture verifies three private
72-byte snapshots byte-for-byte, offset 16 copying, malformed ranges, allocation
failure/reset and O/T/UNKNOWN production. It is not native BG3 scene attachment
or a live quality gate. No ordinary shader changed; GPU pass cost is unmeasured.
Evidence: docs/evidence/omm-owned-vertices-20261030.txt.

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

The backend now also builds/owns the attached material BLAS under the micromap
ticket. The existing six-ray GPU fixture uses this production builder. BLAS is
destroyed before micromap/input storage, after actual completion and command
reference retirement. Actual Vulkan allocation requirements count against the
256 MiB backend budget, including attached BLAS storage and scratch.

The scene validator and all four reference-query branches are implemented and
compile. Fourteen GPU validator cases cover stale/current/unrelated/multiple
image epochs, invalid records and in-progress writers; no native scene caller
or descriptor setup is installed yet. Native source capture is now invoked at
actual eligible static draw bindings, but does not attach micromaps to BG3 yet.

Required next integration: material-specific BLAS variants with
baseline/reference TLAS; scene validation dispatch and shader descriptor wiring;
exact ticket use and
retirement at every scene build/traversal; GPU same-ray known-state comparison;
nonzero useful known coverage and measured BG3 quality/cost with FG active. Higher
subdivision and VT coverage remain open. Level 0 is an initial production wire
format, not the final useful foliage acceleration target.
