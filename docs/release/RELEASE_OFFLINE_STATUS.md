# Offline beta checklist status — 2026-11-03

Scope: remaining offline-checkable items in RELEASE_BETA.md. No game, download,
real install, shortcut creation, package assembly or release approval. PASS below
means only the named offline evidence; it does not complete hardware acceptance.

| Checklist item | Status and evidence |
|---|---|
| Final defaults and specialization reconciliation | OPEN. Final merged configuration and release-defaults review still require owner sign-off. |
| Diagnostics/crash dumps/developer controls off | PARTIAL. `data/release.ini` pins dev, crash_dump and diagnostic keys off. Full effective preset reconciliation remains open. |
| Remove rejected/null water keys | PARTIAL. `rt_water_rr_mv`, `rt_water_fg_mv`, `rt_water_glint_aa` absent from central settings and INI. Final five-experiment review remains open. |
| ls_box / rt_emissive release pins | OPEN. Both are still pinned to 0 in `data/release.ini`; defaults-owner resolution required. |
| README presets, driver/game versions, compatibility claims | OPEN. Compare against final effective settings and qualified receipts; no new runtime evidence here. |
| Package completeness and diagnostics wiring | PASS for source wiring: explicit package list includes Diagnostics.cmd and diagnostics.ps1; all notices enter files.txt. Final archive inspection blocked on licences and packaging approval. |
| Disposable installer checks | PASS for existing release_install_test fixtures: update, original restoration, settings, ownership manifest, protected/linked destinations and incomplete updates. Shortcut and locked-file/interruption acceptance remain OPEN; these tests use NoShortcut. |
| Diagnostics implementation and privacy | PASS for existing diagnostics_package_test fixtures: local ZIP, default no-save, selected synthetic save unchanged, redaction, contents, missing inputs and running-game refusal. No real save read. IncludeSave dialog confirmation remains manual acceptance. |
| README manual report fallback | PRESENT in README, including local-only ZIP and manual attachment instructions. User-facing acceptance remains open. |
| Third-party notices | BLOCKED. Ten DLLs and six compiled SDK components have manifest mappings; available collected texts are verbatim. Selected DXC LICENSE.TXT and Vulkan-Headers LICENSE.md / LICENSES/Apache-2.0.txt / LICENSES/MIT.txt remain missing locally. See THIRD_PARTY.md. |
| Project licence / NVIDIA notification | OPEN owner decisions. No notification sent. |
| Asset DB redistribution | OPEN owner/legal decision; SDK notices do not grant game-data redistribution. |
| Excluded SDKs and tools / SHaRC | PASS for ordinary explicit payload: no Aftermath, OMM SDK, development Streamline binaries, Python or offline tools; SHaRC headers require WithSharc. No approval of that opt-in. |
| Microsoft runtime terms | OPEN redistribution review. Static CRT selected; local VS Redist.txt only points to online terms. No VC redistributable installer/DLL is shipped. |
| Final archive, symbols privacy, upload | BLOCKED on preceding decisions and final package review. No archive assembled or uploaded. |

Fresh-PC/VM installation, shortcuts, WebView UI acceptance, native hair behaviour,
VRAM/long sessions, Act 2/3, other GPUs, overlays/GOG/HDR and live smoke are not
offline passes. They remain open and require separate authorization where applicable.

Validation: `ctest --test-dir build -R '^(package_licences|diagnostics_package|release_install)_test$' --output-on-failure`.
