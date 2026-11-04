# Offline release status - 2026-11-04, stream C

No commit, game launch, upload or installation outside synthetic test scratch. Git metadata is read-only.
PASS means only the named offline check. This is not release approval or final hardware acceptance.

## Owner decisions (questions)

- Which project licence is approved, and should NVIDIA be notified? No notification was sent.
- May the game-derived asset database be redistributed under Larian's terms?
- Are the Microsoft static CRT/WebView2 terms approved? No VC redistributable installer or DLL ships.
- Keep SHaRC off and its headers excluded, or separately resolve its distribution terms?
- Close Fresnel hidden, or accept the retained S8 rim appearance? wsf1 is not promoted here.
- Approve the explicit deferral of the remaining unqualified non-hair backlog?
- What hardware/content support scope is acceptable after VRAM, transition, Act 2/3 and RTX 30/40 acceptance?
- Which installer ships: current ZIP legacy helpers, or separately integrated/qualified lifecycle/Inno?
- Accept ambient ratio 1.44 and the remaining water shadow/glint appearance? Emission stays mode 1.
- After build/acceptance and legal decisions, approve the final archive and upload?

Temporal promotion is already accepted on user visual verdict; it is not a pending owner question.

## Evidence and release docs

| Item | State |
|---|---|
| README / settings | UPDATED to the 15 final controls, three settings pages, stock visible hair, automatic Ultra OMM/masks, Full tracing and Ctrl+Shift+F9 master RT toggle. Removed obsolete player activation guidance. |
| Feature evidence | wf9 08:59 VALID on main 6a00b295: 17 every-window gates; static ON equals OFF; turning compose screen hits about 6.7% to 17%; final compose median 0.154 ms absolute. Receipt read from main checkout `tools/live_batches/water_flicker_wf9/RESULT_wf9_085908.md`, absent in this branch until qualification stream integration. Supersedes the contract/plan's older wf9 hold. |
| Temporal post | PROMOTED by user verdict. RESULT_pairs_032335: isolated 0.152-0.153 ms; 12 primary sign tests p 0.17-0.83 and float energy +0.1492% did not pass. About 250 MiB reference memory comes from release plan, not that RESULT. No statistical pass claimed. |
| Fresnel | NOT PROMOTED. RESULT_wsf1_064436: rim +43%, open water +1.8%, ON variation 13% versus OFF about 2%; surface FS 0.10-0.15 ms absolute; no PT reference. |
| Other historical feature costs | README names OMM, box, bounce and stock-card figures from defaults review/TRACK/native-hair evidence. No new RESULT files for these are in this tree. Older combined RESULT_WATER_185612 is INVALID: exploratory costs do not qualify candidates. |
| GPU / VRAM / versions | No model-based preset guarantee, minimum driver claim or qualified VRAM capacity. Driver 621.01 and game 4.1.1.7631656 remain recorded references, pending final-log verification. Port observation 31.7 GB versus 30.6 GB budget prevents a 32 GB safety claim. |
| Effective defaults / final UX | Contract consumed for documentation. Final merged settings enforcement, generated tables, hidden stale-INI filtering and native/WebView parity remain A/B acceptance; not certified by packaging tests. |

## Full payload audit

Command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -DryRun -NoBuild -NoZip -Version audit-20261104-final -OutDir .scratch/release-package`.
Output: `.scratch/release-package/BG3RTX-audit-20261104-final/` (uncommitted audit artifacts only).

- 227 planned files; zero differences between planned destinations plus files.txt and generated files.txt.
  `payload-list.json`, `files.diff.json` and `audit.json` record the complete plan and availability.
- **One missing input:** `build-nodiag/bin/VkLayer_bg3rtx.dll`. Current launcher/core outputs exist.
  No substitute diagnostic layer was selected, no build was run, and no player payload was copied.
- Independent inventory check: no PDB/ILK, Python, developer EXEs/tools/tests, SDK payload directories,
  Aftermath DLL or development Streamline DLL. Runtime HLSL/common includes remain required.
- 9/9 Streamline/DLSS DLL SHA-256 hashes match production Streamline 2.14.16 `bin/x64`.
  Selected SDK fallback DXC hash matches `1ea6b8a639c43ca2d9e14712962180a887879773e994d8370a087907371be763`.
  Independent hash details are in the first audit's `binary-audit.json` under the same scratch root.
- Required notices, fonts/OFL, branding/provenance, Diagnostics.cmd and diagnostics.ps1 are wired.
  All verbatim notice hashes pass. Notice collection grants no unresolved redistribution rights.
- Packer now copies only shader source extensions/variants.txt, removes duplicate ornament registration,
  refuses existing/linked package output and verifies materialized files.txt against the copy plan.
  The new synthetic complete-package fixture proves plan/materialized manifest equality and excludes
  injected PDB/Python/SPIR-V artifacts. The real dry-run is list consistency, not final archive validation.

## Tests run in worktree scratch

| Check | Result |
|---|---|
| `python tests/package_licences_test.py` | 8/8 PASS, including complete dry-run versus materialized synthetic package |
| `python tests/release_install_test.py` | 7/7 PASS; synthetic install/update/uninstall, original restoration, settings, manifest and path guards |
| `python tests/diagnostics_package_test.py` | 5/5 PASS; local ZIP, default no-save, synthetic selected save unchanged, redaction, missing inputs and process refusal |
| `python tests/release_lifecycle_test.py` | 15/15 PASS; synthetic transactional recovery, rollback and retention; not compiled Inno acceptance |
| `python tests/launcher_diagnostics_test.py <absolute fixture exe> <worktree scratch>` | 55 checks PASS; independent ZIP CRC, exact preview bytes, Unicode destination and junction checks. Initial relative EXE path failed before execution; absolute-path rerun passed. |

TEMP/TMP were scoped to worktree scratch for disposable Python suites. No real save was inspected.
No global cache, Start menu or real install was modified. Source diagnostics stay local; reports upload nothing.

## Still open

Supply/rebuild the lean layer and establish final binary freshness/provenance; inspect a real final archive,
files.txt, private symbol separation and any installer hash manifest after owner decisions. ZIP still ships
legacy install helpers; lifecycle/Inno integration, compiler pin/signing and portable/setup equivalence are
not done. Complete disposable VM shortcuts, locked-file/low-space/interruption and IncludeSave dialog
acceptance with synthetic data. Fresh-PC/no-SDK, Steam/GOG launch and actual launcher accessibility
acceptance remain open. Complete final stock-hair Ultra smoke, dialogue/startup, multi-hour memory,
scene transitions, Act 2/3 and broader hardware acceptance with separately authorized batches.
