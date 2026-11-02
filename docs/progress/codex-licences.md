## Release licence inventory (branch codex-licences, blocked on missing texts, 2026-11-02)

- Audited package copy list, include paths and runtime loads from disk only. No build, game or network.
- Collected 8 verbatim SDK/runtime licence and notice files under docs/release/licences; source paths,
  versions and SHA-256 recorded in THIRD_PARTY.md. All 9 bin/sl DLLs match SDK 2.14.16 production files.
- DXC fallback is 1.9.0.5399 (a107ba613); API header names NCSA, but binary-wide licence and notices
  remain unverified without the matching distribution. No unrelated old sample licence substituted.
- Missing: DXC matching texts; Vulkan-Headers 1.4.357 matching licence/notice bundle. LSLib v1.20.4
  MIT text is conditional (offline tools excluded). SHaRC paused and excluded unless -WithSharc.
- Package wiring now copies THIRD_PARTY.md and every checked-in notice to licences/. Required
  filenames in THIRD_PARTY.md are validated before builds/writes; missing DXC/Vulkan fail loudly.
- Added notice-only -ListOnly and package_licences_test in the existing CMake Python foreach list.
  Packaging stays paused; shader/data payloads unchanged. NVIDIA/Reflex release actions remain open.
- Release texts normalised to LF; inventory SHA-256 updated (source hashes no longer claimed).
- Verification: python tests/package_licences_test.py (4 tests); git diff --check. No commit (requested).
