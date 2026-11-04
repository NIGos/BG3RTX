# Offline release status - 2026-11-04, stream C

No commit, game launch, upload or installation outside synthetic test scratch. Git metadata is read-only.
PASS means only the named offline check. This is not release approval or final hardware acceptance.

## Settled direction and open decisions

- GPL-3.0-or-later plus NVIDIA exception is chosen. No NVIDIA notification is planned.
- May the game-derived asset database be redistributed under Larian's terms?
- Microsoft runtime uses static CRT. No VC runtime installer or DLL ships.
- SHaRC is rejected unless matched injection/resolve inputs exist; keep Off and headers excluded meanwhile.
- Fresnel stays Experimental, Off in presets: invisible on open water to the user.
- Approve the explicit deferral of the remaining unqualified non-hair backlog?
- Only RTX 5090 has been tested. Memory/transition and Act 2/3 acceptance remain open; no broader GPU claim.
- Which installer ships: current ZIP legacy helpers, or separately integrated/qualified lifecycle/Inno?
- Appearance decided by the user (2026-11-04): native ambient kept (S8 ratio 1.44 is not a data error), native S8 water shadow edge and glint kept, rt_hit_emission mode 1, water Fresnel stays Experimental.
- After build/acceptance and legal decisions, approve the final archive and upload?

Temporal promotion is accepted on user visual verdict; its statistical/energy limits remain.
Stock visible hair ships; strands are a post-release Experimental update. One-ray Ultra GI
and Tilesets are being qualified. No promotion is claimed. Manual launcher check: DPI only.
Rejected: three GPU candidates, Medium Half, water bits 2/4, glint, alpha spill (no S8
overflow), GI guards, current Sun and IP grass. Keep work lean and sized to what is needed.

## Evidence and release docs

| Item | State |
|---|---|
| README / settings | UPDATED to the 15 final controls, three settings pages, stock visible hair, automatic Ultra OMM/masks, Full tracing and Ctrl+Shift+F9 master RT toggle. Removed obsolete player activation guidance. |
| Project licence | PASS: root LICENSE and LICENSE-EXCEPTION.md are validated and packaged verbatim; README states GPL-3.0-or-later with the NVIDIA SDK exception in two lines. THIRD_PARTY.md references both. User decision 2026-11-04. |
| Feature evidence | wf9 08:59 VALID: 17 gates; static ON equals OFF; turning hits about 6.7% -> 17%; compose 0.154 ms absolute. Qualified; no release hold. |
| Temporal post | PROMOTED by user verdict. RESULT_pairs_032335: isolated 0.152-0.153 ms; 12 primary sign tests p 0.17-0.83 and float energy +0.1492% did not pass. About 250 MiB reference memory comes from release plan, not that RESULT. No statistical pass claimed. |
| Fresnel | Experimental by user decision; Off in presets. Rim +43%, open water +1.8%; ON variation 13% versus about 2% OFF; FS 0.10-0.15 ms; no PT reference. |
| Other historical feature costs | README names OMM, box, bounce and stock-card figures from defaults review/TRACK/native-hair evidence. No new RESULT files for these are in this tree. Older combined RESULT_WATER_185612 is INVALID: exploratory costs do not qualify candidates. |
| GPU / VRAM / versions | No model-based preset guarantee, minimum driver claim or qualified VRAM capacity. Driver 621.01 and game 4.1.1.7631656 remain recorded references, pending final-log verification. Port observation 31.7 GB versus 30.6 GB budget prevents a 32 GB safety claim. |
| Effective defaults / final UX | Contract consumed for documentation. Final merged settings enforcement, generated tables, hidden stale-INI filtering and native/WebView parity remain A/B acceptance; not certified by packaging tests. |

## Full payload audit

Build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -ReleaseLayer`.
Command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -DryRun -NoBuild -NoZip -Version audit-20261104-pack2 -OutDir .scratch/release-package`.
Output: `.scratch/release-package/BG3RTX-audit-20261104-pack2/` (uncommitted audit artifacts only).

- 229 planned files (previously 227); zero differences between planned destinations plus files.txt and generated files.txt.
  `payload-list.json`, `files.diff.json` and `audit.json` record the complete plan and availability.
- **Zero missing inputs** (previously one). `build-nodiag/bin/VkLayer_bg3rtx.dll` built in 23 s
  at BelowNormal, RelWithDebInfo, diagnostics OFF. The new build switch reasserts configuration
  on every invocation; packaging uses it. No player payload was copied by the real dry run.
  Layer SHA-256: `2b987020f17997e94d955cc4cb5724487e5c5ad512eb73d2f57b3c9970605b66`.
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
| `python tests/package_licences_test.py` | 8/8 PASS rerun for release-pack2, including both verbatim root licences and complete dry-run versus materialized synthetic package |
| `python tests/release_install_test.py` | 7/7 PASS rerun for release-pack2; synthetic install/update/uninstall, original restoration, settings, manifest and path guards |
| `python tests/diagnostics_package_test.py` | 5/5 PASS rerun for release-pack2; local ZIP, default no-save, synthetic selected save unchanged, redaction, missing inputs and process refusal |
| `python tests/release_lifecycle_test.py` | 15/15 PASS; synthetic transactional recovery, rollback and retention; not compiled Inno acceptance |
| `python tests/launcher_diagnostics_test.py <absolute fixture exe> <worktree scratch>` | 55 checks PASS; independent ZIP CRC, exact preview bytes, Unicode destination and junction checks. Initial relative EXE path failed before execution; absolute-path rerun passed. |

TEMP/TMP were scoped to worktree scratch for disposable Python suites. No real save was inspected.
No global cache, Start menu or real install was modified. Source diagnostics stay local; reports upload nothing.

## Still open

Lean layer supplied; establish final payload binary freshness/provenance and inspect a real final archive,
files.txt, private symbol separation and any installer hash manifest after owner decisions. ZIP still ships
legacy install helpers; lifecycle/Inno integration, compiler pin/signing and portable/setup equivalence are
not done. Complete disposable VM shortcuts, locked-file/low-space/interruption and IncludeSave dialog
acceptance with synthetic data. Fresh-PC/no-SDK and Steam/GOG launch acceptance remain open. Manual launcher acceptance
is DPI only. rel1 Ultra smoke VALID 13:47, rel2 High VALID 12:13 and rel3 stock hair VALID
13:51 are recorded receipts; review retained dialogue images separately. Do not repeat
smoke without a material release change. Memory/transition and Act 2/3 acceptance require
separately authorized, lean batches. Only RTX 5090 has been tested.
