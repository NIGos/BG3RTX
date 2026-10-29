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
