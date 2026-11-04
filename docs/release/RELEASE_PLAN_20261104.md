# Release plan, 2026-11-04 (stock hair)

Release with stock visible hair and qualified features. Strand hair is a post-release
update on the Experimental page. Keep work lean and sized to what is needed.
The [control contract](CONTROL_CONTRACT.md) defines final labels, keys and dependencies.
The [backlog](../BACKLOG_20261104.md) records feature evidence and remaining dispositions.

## Selected configuration

- Four presets: Low, Medium, High and Ultra, plus a Custom indicator.
- Full trace resolution everywhere. Medium Half is rejected (~0.25 ms saving).
- Ultra keeps hidden roofs, Full local/box shadows, Rough reflections, water with
  refraction, Far bounce at 6 m, emission mode 1 and stock hair/scalp capture.
- One-ray Ultra GI and Tilesets are being qualified. Keep two-ray GI and the
  Tilesets choice hidden until accepted; neither is a quality pass yet.
- Temporal post is accepted for Ultra by user visual verdict. Cost 0.152-0.153 ms;
  reference memory about 250 MiB. The 12 statistical tests were inconclusive
  (p 0.17-0.83), and float energy +0.1492% failed. Preserve those limitations.
- Exact-k k1 is automatic for qualified Ultra Full local shadows/RT geometry:
  S7 0.717 -> 0.555 ms (-23%); energy within OFF spread 0.89%.
- OMM/dynamic OMM/fine LOD and local masks are automatic for High/Ultra Full
  geometry. Retain hardware and missing-mask fallback.
- Stock-hair pool trim is automatic when capture applies; mesh pool -26-50%.
  No isolated GPU timing is claimed.
- Water camera correction follows water. wf9 is qualified, not a release blocker.
- rr_post=1; Reflex On; supported FG 2x; cap Off except Ultra 58 rendered fps.

## Experimental and rejected work

Show experimental options is Off by default and opens a separate Experimental page.
Fresnel stays there, Off in presets: the user sees no open-water difference.
wsf1 passed automated gates: rim +43%, open water +1.8%, surface FS 0.10-0.15 ms;
ON variation 13% versus about 2% OFF. This does not imply automatic promotion.
Strand appearance, coverage, shadows, GI, crowd and GPU safety remain post-release work.

Rejected: compact hit state, alpha metadata cache and empty water tiles (image gate
failed; savings 0.53/0.11/0.19 ms); Medium Half; water bits 2/4; glint; alpha spill
(no overflow at S8); GI guards; current Sun replacement; IP grass.
SHaRC is rejected unless matched injection/resolve inputs exist. Keep it Off and its
headers excluded while that condition is unmet. Do not rerun rejected candidates.
Keep removed hair resolve, static-only cascade and null water keys absent.

Hidden-water shading and sparse bit 1 still need separate appearance/equivalence
gates. Generated rows, pak publication, hidden-reflection AO, lava/HeightGradient/
HLOD, texture publication and other unqualified work need explicit dispositions.
Implementation alone is not qualification; see the backlog for bounded next steps.

## Qualification and acceptance

| Receipt | Status / deciding evidence |
|---|---|
| wf9, 08:59 | VALID; 17 gates; static unchanged; turning hits about 6.7% -> 17%; compose 0.154 ms absolute |
| rel2 High, 12:13 | VALID; qualifies OMM/masks on High |
| rel1 Ultra, 13:47 | VALID integrated smoke |
| rel3 stock hair, 13:51 | VALID; review retained dialogue images separately |

Do not repeat these receipts without a material release change. Strand work does
not require a new stock-hair release smoke by itself.
Only RTX 5090 has been tested. Do not claim other GPU or VRAM qualification.
Memory/transition and Act 2/3 acceptance remain open. The old ~43-minute long-session
job exceeds the 15-minute launch cap. Size acceptance to the needed observable;
shorter launches alone do not prove multi-hour retention.
New live batches require fresh explicit steering, current lint and every-window
gates. Prepare outside watched inboxes; use the serial queue. No builds or heavy
compiles during cost windows. Manual launcher acceptance is DPI only.

## Offline release closure

Verify final defaults/schema/generated tables, preset layering, stale-INI filtering,
prerequisites and native/WebView parity. Keep meaningful controls on Lighting,
Surfaces and Performance; omit empty Advanced navigation. Help & support holds
compatibility, notices and local reports. Play saves first; keep Ctrl+Shift+F9.
Use the control contract for apply transitions and automatic policy details.

Supply the lean layer, verify final binary freshness/provenance, and inspect the
final archive, files.txt, runtime DXC, branding/fonts/licences and private symbol
separation. Verify LICENSE and LICENSE-EXCEPTION.md inclusion. Offline package
checks do not certify the final archive.
Installer choice and disposable install/update/uninstall, locked-file/interruption,
fresh-PC/no-SDK and launch acceptance remain open. Use synthetic save data only.

GPL-3.0-or-later plus the NVIDIA exception is chosen. No NVIDIA notification is
planned. Microsoft runtime uses static CRT; no VC runtime installer or DLL ships.
Game-data redistribution rights remain open. No release or upload approval is implied.
Other open decisions: oversized Git blob remedy, remaining backlog deferrals,
installer lifecycle, ambient ratio 1.44 and remaining water appearance, final archive/upload.

## Ownership and dependencies

Settings owns central metadata/defaults and policy; Launcher consumes that contract;
Packaging/docs consumes final defaults and UX; Qualification owns jobs and receipts.
Use disjoint progress fragments. Coordinate shared registrations; serialize heavy
DXC. Merge only through merge-green outside lives, after reading deciding numbers.
