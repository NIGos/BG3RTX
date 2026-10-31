# D-EM coincident draw hypothesis

P1 remains open after293; its default-off candidate is parked, not promoted.
The AF exact emissive family304be1c7 still fails0.361/0.362 versus its derived
0.842..1.187 interval, while GF passes0.999. UV location and pulse phase were
already checked on the preserved legacy branch9f4d17d. Do not tune radiance,
calibrate this exact family, narrow the bright cohort, or waive the AF failure.

The native fragment shader directly samples its location0 UV and emits a pulse
modulated colour. Legacy capture shows three draws of this family in AF. The
current model asks only for the closest emissive triangle; its previous overlap
test considers a second emitter strictly IN FRONT, excluding equal-depth draws.
Thus an additive stack at the same surface is an untested possible cause.
This is a hypothesis, not evidence of a missing factor3 or permission to scale.

codex/p2-emissive starts at current main6514a13 and cherry-picks a consolidated
D-EM-only source change through9f4d17d (source import71fcd4c, result69b4eaa).
Original branch/history/tags remain unchanged. Main's newer rules/reference and
QA exposure-hold fix are retained. Legacy handoff is preserved separately.
rt_emissive0 is development/release default, with no emissive scene allocated
when disabled at device creation. Enabling requires startup; disabling capture
remains live. Paired host/core ABI9 is D-EM-specific: never reuse P3's ABI9 process.

Diagnostic rt_em_overlap_check0 (default/release off) reuses reserved Push/debug
stat words8..14. For each current receiver whose nearest emissive hit is the
chosen rt_em_dbg_fam, an uncommitted nonopaque ray query enumerates candidates
in the SAME existing depth interval. It reports target receivers, same-family
triangle candidates, receivers with multiple DISTINCT geometry/draw IDs, the
distinct-draw sum, invalid IDs, explicit eight-ID storage overflow and missing
candidates. It changes no emission, calibration, light list or query used by a
consumer. Triangle candidates and distinct draws are different quantities.
The legacy interval is broad (rt_em_tol); multiplicity alone does not prove
coplanarity, identical UV/emission, native visibility or additive contribution.

Actual draws additionally log their pipeline's attachment0 blend equation,
write mask, culling and depth state. Relevant dynamic state makes the logged
static state INCONCLUSIVE. A creation record without an actual draw is no proof.
Native additive blend plus coincident draw counts would motivate a per-hit
contribution test; it does not itself justify summing all triangles or changing
production emission. Shared raster edges, culling, different depth and material
identity must be respected before any physical fix.

The fixture uses startup controls and the same live session settings; statistics
come only from loaded-scene windows. Debug0 preserves normal output. The legal
existing exposure hold is applied after scene-ready, then warmup12s. Full startup
log supplies draw-state proof; whole VVL and capture finite checks remain required.
No comparison of independent moving-run images is used.

SDKs use explicit CMake paths and an owned small SHaRC copy; asset DB is an owned
copy. No dependency/data junction is created. Offline build/3 CTests and all36
shader files pass before runtime; ordinary shader parity is checked separately.

## Run 295 audit: additive overlap rejected

The preserved run completed execution-valid on ca1959b, before P1 resumed.
Ten scene-only windows contain 67,031 target receivers and 71,754 triangle
candidates. Multi-draw receivers are zero; the distinct-draw sum is exactly
67,031, with zero invalid IDs, overflow or missing candidates. Extra candidates
belong to one geometry/draw, not an observed additive stack.

Nine actual native draw-state records show pipeline 34445, fragment shader
304be1c71df194cb, blend disabled, back-face culling, depth test EQUAL, no depth
write and no relevant dynamic state. This rejects the tested additive-draw
hypothesis for that observed cohort. Do not multiply radiance by draw count.

The exact-family gate still FAILS: ratio 0.362 against 0.842-1.188, 237 bright
pixels on average. The other five evaluated families pass this short diagnostic.
Both final captures report zero nonfinite values. Native D32F dumps report
2,764 / 2,587 nonfinite values, preserved as a limitation; exported raw arrays
contain none. No scoped layout/push/write VVL tags, but other VVL errors remain.
Build median 0.496 ms (p99 0.756); effects passes 0.118 ms with capture. No
without-capture baseline was sampled, so incremental overhead is not established.

Full evidence: docs/evidence/p2-emissive-overlap-295.json. Full VVL is preserved
at build/p2-emissive-overlap-295.validation.log. The old job used hold 45; future
120-frame jobs must use hold at least 90. This observation is not a promotion gate.

## Next smallest hypothesis: facing policy

The native shader was re-read from the preserved SPIR-V. It samples raw location-0
UV at set 1 binding 1, combines colour/mask/intensity, and applies the captured
pulse. No missing additive term appears. Existing constants and UV checks remain
unchanged. Native sampling state is not recorded by the current trace, so repeat
versus clamp is still unverified; do not treat filtering tests as its proof.

A more concrete mismatch is present in the emissive query: the native draw culls
back faces, while the emissive TLAS disables facing culling and lookup forces
opaque, two-sided hits. Multiple candidates from ONE draw could still select a
surface native rasterization culled. This is a hypothesis, not its demonstrated
cause. Reuse the existing debug query/stat slots to count committed facing for
AF and the passing GF control before changing emission. Native front-face state
and captured transform winding must be respected; never guess a sign or fit a
scale. No new resource or ABI is needed for a facing-only probe. Keep off until
the original exact-family gates, validation and moving checks pass.
