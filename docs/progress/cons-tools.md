## Shader tooling consolidation (branch cons-tools, kept, 2026-11-04)

- F01 confirmed: an existing corrupt blob survived recompilation and was marked verified. Verify its SHA256 before reuse; atomically repair it and never certify a failed replacement. Regression reads compiler bytes and reopens the cache for a verified hit.
- F05 confirmed: pp verification excluded failed compilations. Either-side compile failure now fails the sample; tests cover cached, fresh and both failures.
- F18 confirmed: absent GI/RF interface checkers silently skipped qualification. Required missing checkers now fail, including emission-only GI requests. CLI regressions use successful fake compiles and absent GI/RF executables.
- F19 confirmed: checkout bytes were hashed before DXC read mutable paths. Compile and preprocess now materialize captured checkout and SHaRC bytes only on misses; keys and cache schema stay unchanged. Regression mutates both inputs after capture and checks actual command input paths/bytes.
- Validation: `python tests/shader_identity_test.py` passed 77 checks; scoped `git diff --check` passed. No DXC workload, game run, generated DB, commit or shader request delta.

## Tooling QA and build consolidation (branch cons-tools, kept, 2026-11-04)
- F02 confirmed: zero-ratio bright families fail; empty/no-argument/insufficient emission evidence fails.
- F03 confirmed: nonfinite scoring compares each dump with OFF counts using full shader identity, format, slot, dimensions and occurrence; 1 OFF vs 1000 ON now fails by 999.
- F04 confirmed: missing planned traces, manifests, images, final frames or temporal repeats mark the result INVALID despite successful metadata. Missing raw evidence without a saved result is analysed as INVALID.
- F06 confirmed: asset discovery wrapper registers every portable tools/assets/tests module in CTest, including future modules; install-dependent skips remain explicit.
- F07 confirmed: all 8 omitted QA suites and qa_detectors are registered; root PYTHONPATH makes direct CTest entry points import reliably. Registration audit protects the named suites.
- F20 confirmed: cached CMAKE_BUILD_TYPE must match -Config; absent cache/type also reconfigures. Mocked regression covers Release vs RelWithDebInfo, match, absent cache, empty and missing key.
- Validation: 77 existing QA unittest methods, 12 QA evidence methods, build configuration regression and registration audit pass; standalone QA detectors pass.
- Full build -Test built 309 steps in 82 s; 170/172 passed initially. Asset fixture was corrected; water gate cache was redirected into the writable worktree. Both failures plus the final QA suite passed a targeted 3/3 CTest rerun (4 s).
- Removed six generated PDB/ILK artifacts and four copied SDK DLLs above 20 MB; shared SDK originals untouched. No game launch, commits, renderer source changes or shipped database rebuild.

## Asset consolidation (branch cons-tools, kept, 2026-11-04)
- F08 confirmed: repeated, negative and out-of-range attribute links now raise LsfError; parent cycles also fail.
- F09 confirmed: section spans, record strides, names, values and node indices validated. Real install fixture requires valid compact format 2, retained alongside formats 0/1.
- F10 confirmed: all compression dispatches enforce decoded length; LZ4 fallback bounds every input/output span; optional frame/zstd decode uses output limits.
- F11 confirmed: patch precedence uses the complete numeric tuple; Patch 9 overrides Patch 8 HotFix 30.
- F12 confirmed: rejected and missing selected packages raise explicit PackageError; incomplete package selection cannot publish a DB.
- F13 confirmed: legacy timestamp slot stores a deterministic content stamp, source digest derives from SHA256 of package/part content, excluding mtimes; wall time remains manifest metadata. Version 1 and C++ reader layouts unchanged.
- F14 confirmed: hashed module records sorted before variant allocation; jobs 1/2/4 and changed package mtimes produce identical whole DB bytes with populated MVAR/MPRM/SREF fixtures.
- F15 confirmed: material/effect worker caches keyed by canonical directory and selected package names; serial A/B installation fixture passes.
- F16 confirmed: reader rejects invalid TOC spans, versions, overlaps, strides/counts and nonlocal/nonboundary STRS references.
- F17 confirmed: preset parse failures counted and logged; unresolved world/trigger/probe lighting and atmosphere resources and trigger links fail before publication. Existing missing SH counters retained.
- Validation: python -m unittest discover -s tools/assets/tests -t . passes 127 tests, 1 optional zstd skip; real install read-only fixtures pass. No shipped DB regenerated, no game, no commit.
