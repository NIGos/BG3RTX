## B2 shared diagnostics and suitability (ux-b2-diagnostics, offline verified, 2026-11-02)

- Added UI-independent collector in `tools/launcher/diagnostics.{h,cpp}`.
  `prepare_report(Request, Progress)` reads bounded, shared text snapshots only;
  returns exact sanitized preview files, sizes, privacy text and CONTENTS.txt.
  `save_report_zip(Snapshot, path, Progress)` exports those frozen bytes as ZIP,
  create-new only. Retry retains the snapshot/issue. `exit_code` maps 0/1/2/3
  to Complete/Partial/Failed/Cancelled. Run callbacks on a worker thread.
- Request selects logs/settings/system groups; attachments are unsupported.
  Fixed allowlist: BG3RTX root logs, INIs, VERSION.txt, flat sl_logs text files.
  No launcher INI, save discovery, binary dumps, screenshots, network or uploads.
  Redaction follows diagnostics.ps1 profiles/credentials, adds known install paths
  and omits environment/command-line lines. Preview remains best-effort redaction.
- Default bounds: 4 MiB/file, 16 MiB/report, 32 files; hard limits 8/32 MiB/64.
  Oversized/locked/invalid-text/missing/stale inputs produce partial reports.
  Manifest gives per-file UTC timestamp; >24 hours is a stale-data review hint.
  Effective config uses latest current-log cfg lines, never claims current saved state.
  Defaults carry CRC32 identity (not a security hash). No empty-success report.
- `inspect_graphics`, `assess_compatibility`, `graphics_report` are the A2 API.
  Probe checks actual ray-query/AS/address features, driver and local heap/budget.
  Unknown caps are 0; multiple adapters need explicit selection. Game adapter
  remains unconfirmed. A1 gpu_caps/suggest_preset policy is reused unchanged.
  README 12/16/24/32 GiB guidance is labelled risk estimates, never qualification.
  621.01 is a tested reference; no verified minimum exists, others are Untested.
- A2 owns Report button/--report dispatch: inspect -> prepare -> privacy and exact
  preview -> chosen ZIP path -> export. Default folder: Documents/BG3RTX reports.
  No UI or dispatch changed here. C2 must add diagnostics.cpp/compatibility.cpp
  to the launcher target and register fixtures; shared CMake was not edited.
- Isolated check: configure tests/launcher_diagnostics into build/b2-fixtures,
  build Release, ctest -C Release: 55 C++ checks plus Python ZIP CRC/exact-preview,
  Unicode paths, fixture junction rejection and unlink-only cleanup. 1/1 passed.
- No game, real installer, saves, shader tools, ABI or rendering changes; shader
  request delta 0. No screenshots: B2 owns no launcher window; A2 provides UI QA.
  Git metadata untouched; no commit. PRODUCT_REVIEW read from codex-product-review
  with git show because it is absent in this checkout. No branding changes.
