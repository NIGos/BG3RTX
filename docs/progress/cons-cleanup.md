## Rejected option cleanup (branch cons-cleanup, tests PASS, raw identity FAIL, 2026-11-05)

- Removed compact hit state, alpha metadata cache, empty water tiles, water hit shading/cost, alpha spill, Pak publication, generated rows, IP grass, Half trace, current-sun capture and GI guards.
- Ordinary static/wind, Full trace, sun/cascade/OMM and overflow fallback remain. Retired IDs/counters/callbacks stay reserved; ABI 26 unchanged.
- SHaRC device capability kept: shipped water and retained hair share shaderInt64 setup. One-ray files and still-used offline Pak tools remain; Native Tilesets hidden/off.
- Manifest: 167 -> 122 requests, delta -45 (water -10; generated/IP grass -19; Half -14; current sun -2). Proof adds no requests.
- Each removal built. Original build.ps1 -Test: 152 CTests PASS, 164 seconds; defaults/schema/launcher checks PASS. Unknown old INI keys have no consumers.
- Jobs: 97 survive; readiness rows 330 -> 184, regex rows 391 -> 371. Mixed water smoke/wf9/post fixtures retain existing measurements.
- Original raw identity vs bundle35: 79/214 identical, 135 different, no compile failures; receipt `.scratch/cons-cleanup-identity-remaining.json`.
- Fresnel default equivalence PASS after symmetric capability trimming. Source subset: 4,009 lines removed / 230 added; detailed removal history remains in HEAD and TRACK outcomes.
- Standalone queue-child permission check incomplete because pwsh is absent; synthetic PS5 child transport passed.

## Release-path equivalence proof (branch cons-cleanup, PASS, 2026-11-05)

- Shared `tools/spirv_equivalence.py` serves the cascade gate and `tools/release_shader_equivalence.py`; one specialization/optimization/canonical comparison implementation.
- Baseline `1a7e461c`: 214 requests; 71 identical, 139 equivalent, 4 DIFFERENT. Negative cascade control PASS.
- Full run 514.1 seconds; cached confirmation 4.7 seconds, same counts/control/coverage. Shared compile cache and DXC slots retained.
- Removed IDs 1001, 1002, 1005, 1006, 1016, 1021, 1022, 1023 and 1024 all specialize to release value 0.
- Retained Boolean domains cover presets and opt-in choices; tilesets policy-forced off. Address constants remain symbolic, never frozen to artificial zero addresses.
- Covered 6,388 states: 236 frozen plus 143 symbolic pair checks; 139 universal entry certificates. Failed symbolic equality falls back to every combination; identical bytes skip optimization.
- Eight baseline release requests restore `GI_SPEC_GUARDS=1`, explaining 71 identical here versus 79 in the raw same-spelling comparison.
- Four differences are existing merged hair safety fixes from `5f3b8a88` / `b858db78`: groom bounds, count tags, fixed-depth G-buffer rays and velocity ownership.
- These fixes are already on main (bundle36, reviewed hair work). Against main ce1448bf the hair requests are identical (`--baseline ce1448bf --filter hair`: 6 identical, 3 equivalent, 0 different, control PASS, 39 s). Release-path proof complete: 0 different.
- Per-request states and first differing instructions: `.scratch/cons-cleanup-equivalence.json`; original full-run receipt: `.scratch/cons-cleanup-equivalence-full.json`.
- Shared helper/CLI tests: 124 checks PASS; cascade tests: 7 PASS.
- Final `scripts/build.ps1 -Test`: PASS, exit 0; all 167 CTests passed in 213.62 seconds. Receipt: `.scratch/cons-cleanup-equivalence-build-test-verified.log`.
- Initial vswhere PATH warning and progress-fragment length failure corrected; targeted `python tests/fold_progress_test.py` also PASS.
- Regenerated debug/linker outputs over 20 MB removed after tests; verified binaries and receipts retained.
- No commit or game launch; no new repository file exceeds 20 MB. ABI 26 unchanged; proof request delta 0.
