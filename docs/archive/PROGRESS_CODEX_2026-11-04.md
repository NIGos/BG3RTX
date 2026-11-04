# BG3RTX progress - 2026-11-04

## Current user decisions

- Stock visible hair ships. Strands are a post-release update on Experimental.
- Fresnel stays Experimental, Off in presets: invisible on open water to the user.
- Temporal post is accepted for Ultra by visual verdict. Cost 0.152-0.153 ms;
  statistical tests remain inconclusive and float energy +0.1492% failed.
- One-ray Ultra GI and Tilesets are being qualified; current defaults stay until accepted.
- Rejected: three GPU candidates, Medium Half, water bits 2/4, glint, alpha spill
  (no overflow at S8), GI guards, current Sun and IP grass. SHaRC is rejected unless matched inputs exist.
- GPL-3.0-or-later plus NVIDIA exception chosen; no NVIDIA notification planned.
- Microsoft runtime uses static CRT; no VC runtime installer or DLL ships.
- Only RTX 5090 tested. Manual launcher acceptance is DPI only. Keep work lean.
- Open: game-data rights, remaining backlog deferrals, installer lifecycle, oversized Git
  blob remedy, ambient ratio/water appearance, final archive/upload and acceptance below.

## Water compose CPU refusal diagnostics (branch water-compose-reason, OFFLINE_VERIFIED, 2026-11-04)

- Append diagnostics to the existing compose stats line; preserve its previous wording and queue regexes.
- Report actual dispatched hit-shade specialization 0/1/2 pass counts, including cached or pending replacements,
  and passes whose effective flags contain 64. No gate, shader, setting, ABI or default behavior changes.
- Count requested-but-refused light-binding, input-lifetime, scene/material and frame/shift failures.
  Reasons can overlap; first mismatching light-list index is 0..6 (shader bindings 22..28), or -1 if none compared.
- Core build passed. CPU water/core and Python queue/water/live gate CTests: 11/11 passed.
  Queue regression covers the appended fields while retaining existing compose gate matches.
- Shader request delta: 0. No game launch or Git metadata writes; no commit created.
- GPU cost and the wos1/wo1 refusal cause remain unmeasured. On a separately authorized S8 run with
  rt_water_hit_shade=2, inspect the appended CPU fields alongside compose hit attempts; this is diagnostic evidence,
  not a quality qualification or a relaxed lifetime guard.

## Water compose binding and lifetime gates (branch water-binding-fix, offline verified, 2026-11-04)

- wos1 16:09:16: spec 2 selected 0/60 passes; light/input refusals 60/57.
  Forward F was checked against the deferred F-1 stash, first at exposure b22.
  Those are different pass contracts, not descriptor-set handle comparisons.
- Observe the complete deferred tuple at dispatch pre, before the delayed flush.
  F-1 borrowing requires all nine resource descriptors and the game set to be
  re-observed in F; missing/rotated resources still refuse. Keep scene/material
  retention, the one-frame bound and exact build-camera shift checks.
- Forward equality now controls shared WATER_LS bindings only. On a mismatch,
  bind the complete deferred tuple and preserve native local-shadow energy.
  Same-frame hit shading no longer depends on forward descriptor equality.
- Core-only build passed. Ordering regression covers all nine rotations, changed
  game sets, incomplete snapshots, forward-before-flush and missing camera shift.
  All seven water CTests pass; water compose reports 1,203,994 checks.
  Water surface shader CTest passes (492 s with a fresh worktree-local cache;
  the read-only shared cache initially failed to open).
- No shaders, variant requests (+0), settings, presets, host ABI or layer changed.
  No Git metadata written and no game launched. GPU cost remains unmeasured.
- Live confirmation remains pending: S8 with rt_water_hit_shade=2 and
  rt_water_compose_shift=1 should select spec 2/flag 64 when the deferred lifetime
  witness matches, with nonzero qualified hidden/offscreen hits. Rotating deferred
  tuples remain conservative fallbacks. The old log does not include descriptor
  values, so the precise resource/offset/range difference is not established.

## Launcher UI polish for release (branch ui-polish, offline verified, 2026-11-04)
- Choice buttons size to their labels (no word breaks); a card too narrow for them shows a dropdown
  instead (`fitChoices`, re-run on resize/zoom). Checked at 100/150/200% and 736 px.
- Frameless WebView window: native slim title bar (icon, name, version, min/max/close) painted by the
  host; WM_NCHITTEST returns caption/button/top-edge codes, so drag, snap, the Windows 11 snap layouts
  flyout and the system menu work. Per-monitor DPI v2 for this window only (WM_DPICHANGED handled).
  The native fallback (`--native-ui` / no WebView2) keeps the standard Windows frame.
- Requirements: one deduplicated "Needs ..." line per card, unmet only, short user-facing names; met
  hardware never shown. The full list (grouped by choice) stays in "Details and requirements".
- Clean card grid (equal heights per row, apply badge, measured cost as a muted line), inline stroke
  icons, new original logo (gem struck by a ray; `assets/branding`, ICO regenerated by export-branding.py).
- Release "Show experimental options" switch (off by default): Experimental page with the 13
  `settings::kOptInKnobs`; saved as `experimental_options=1`; Off restores Off (release policy).
  Rejected candidates, perf_* keys, diagnostics and policy-owned knobs stay developer only. dev=1 unchanged.
- No renderer, shader or preset change; request delta 0. Manual check left: real-window DPI moves
  between monitors, snap layouts hover, high contrast.

## Release launcher copy and layout (branch ui-copy-apply, kept offline, 2026-11-04)
- Applied approved copy to 15 controls; shared Lighting order, Surfaces blur smoothing, compact header, single costs, notices links and Play saves first. Keys/presets/dependencies/apply modes and hidden metadata unchanged; shader request delta 0.
- MSVC build and 6/6 targeted CTests pass: settings 4,422; bridge 987; diagnostics 59; UI state 33. Native report input parity and interactive accessibility/zoom acceptance remain open; no game launched.

## Tileset qualification jobs (branch ts-fix, READY_FOR_LIVE, 2026-11-04)

- ON diagnostic enables rt_debug=9 with rt_stats=1 to populate Tileset counters;
  cost, shot and restore explicitly reset debug to 0. No rendering/ABI changes.
- Unknown kind 5 records retain UNKNOWN/aborted fallback (missing/unsupported
  inputs); report their count. Gate complete > 0, capture-incomplete 0 and
  positive qualified family2/native primary participation.
- OFF 175623 cost already contained RT 9.242 ms / GI 11.583 ms for 60 frames.
  Unconditional first-sample removal caused INVALID; shared reset-aware
  selection now retains the first complete cost and diagnostic periods.
- Regenerated jobs/plans: 2/2 preflight lints pass, 90 unittest checks plus
  standalone queue/analyzer checks pass. No frozen corpus changes needed.
  No live queued or launched; rendering quality and new ON cost remain pending.
- User check: authorized ts1 OFF/ON on Test Dialogue3, inspect retained PNG pair
  and descriptive float energy/coverage/cost deltas; no quality pass claimed.

## Trace window sizing (branch trace-sizing, kept offline, 2026-11-04)

- Trace serialization and capture duration sizing now use the shared capture-stall
  model: maximum of the declared floor, job evidence and current VALID p95.
- Regenerated affected source jobs and combined manifests/plans, including gi1,
  Tilesets and hv9. gi1's four shot-rays windows: 14.1 -> 14.2 s each, above
  the live exclusive minimum of 14.107 s. Cost windows: 2.4 -> 3.3 s;
  pan windows: 4.7 -> 4.9 s. No gates, trace counts or camera paths changed.
- Frozen snapshot changed only trace_capture_stall: p95 2.61315 -> 2.65112 s
  (359 samples). Other frozen derived values remain unchanged.
- Fresh live corpus: 75 VALID logs; active lint 11/32 failing before -> 0/32
  after. Additional gi1, Tileset OFF/ON and hv9 checks: 0/4 failing.
- batch_jobs_lint_test: 7 tests; live_flow_test: 69 tests; queue_gates_test:
  all checks and 8 unit tests pass. Added serialized measured-stall/reload coverage.
- Offline preparation only. No game launch, queue staging, Git metadata writes
  or generated corpus files added to Git. Rendering quality and GPU cost unchanged.

## hv9 and ts1 runner repairs (branch trace-sizing, kept offline, 2026-11-04)

- hv9 refusal: timed return-camera declared frames=0, rejected by the runner.
  Omit frame fields there; shared sizing retains five reset-aligned 60-frame
  windows (stats_reset=true, settle_frames=0, one period). Regenerated plan 66.6 s.
- Both ts1 startup states now use dev=1 to preserve the hidden Tilesets choice.
  Release Ultra values otherwise retained; existing cfg-key expectations require
  rt_native_tileset=0 OFF and 1 ON. Regenerated both jobs/plans and input hashes.
- Live-corpus lint: 75 VALID logs, active 32/32 and targeted hv9/ts1 3/3 pass.
  batch_jobs_lint_test: 7 tests pass; queue_gates_test: all checks and 8 tests pass.
- No game launch, queue staging, commit or Git metadata writes. Live confirmation
  remains pending; no rendering quality or GPU cost claim.

## Tileset release choice prepared (branch tileset-qual, READY_FOR_LIVE, 2026-11-04)

- Prepared ts1-native-tileset under tools/live_batches/tileset_qual; no game, queue or Git writes.
- Retained inventory: Dialogue3 132 Tileset G-buffer draws, 48 supported material draws and 56 supported depth draws; largest identified save among 101 retained captures.
- rt_native_tileset is pinned on every config reload. Two separate OFF/ON launches are required; a live 0/1/0/1 would be false evidence.
- Release Ultra + Hair shadows; each plan 99.6 s plus 120 s load allowance, 3.66 min/launch, hard cap 4.9 min. Two static PNG traces, estimated 140 MB.
- Gates: ON complete records and GPU family2/native matches positive, unknown/incomplete 0; scene timeouts, RT/alpha overflow and material table overflow 0; coverage and RT/GI GPU ms per state.
- Float pre-post energy/exposure reported; no matched Dialogue3 PT reference found. Cross-launch view/exposure confounds remain; energy and quality await the user's look at both static PNGs.
- Offline: 2/2 job preflights, scene-wait/capacity lint, both fake-queue negative and positive paths, and 3 synthetic tests pass. Receipts: tools/live_batches/tileset_qual/validation.json and lint.json. Historical replay is UNPROVEN.
- Pending live evidence is not a quality pass. Tilesets stays off until the user accepts it. ABI and shader requests unchanged (+0).

## Reset-aligned frame windows (branch short-windows, offline verification, 2026-11-04)

- Measured comparisons use settling frames plus one complete stats period. FPS only estimates seconds and launch caps.
- rt_stats_period_frames=0 preserves native cadences; prepared jobs request 60. rt_stats_reset is an acknowledged token edge.
- The runner waits for settings/pipelines, settling frames and the exact reset acknowledgement. The first complete period counts; legacy logs retain the straddle skip.
- Pending GPU samples are discarded on reset. Windows wait for completed producer lines and requested trace exports before changing settings.
- Standalone reconstruction settling uses 32 actual frames, or 108 for temporal EMA decay below 1/1024.
- Periodic structural gates need one complete period. Repeated OFF/ON windows and energy capture counts remain; no active noise derivation requires a period above 60.
- No shader or ABI change. shaders/variants.txt request delta: 0. Default/release rendering settings remain unchanged.
- Estimated scheduled seconds (excluding load/readiness): hv7 303.9 -> 44.4; wos1 161.3 -> 131.3; rel1 99.8 -> 89.9. These are cap estimates, not live timings.
- gi1 and ts1 are absent from this checkout's jobs/generators; their duration comparisons need source paths.
- Active lint 31/31 and additional wos1/rel1/rel3 lint 3/3 pass. All six combined dryruns pass.
- Full layer/core build passed; settings 4,736 checks, targeted CTests 6/6, live_flow 66, live_guard 23, queue-gate regressions and camera plan 687 passed. Runner fields validated for all 31 active jobs plus wos1/rel1/rel3.
- Trace-only capture windows retain their requested events and wait for actual exports; FPS estimates cannot advance them early.
- Pool/pak lifetime totals retain snapshot semantics; they do not establish fresh attempts in a reset period.
- No game launched, live evidence collected, Git metadata changed or commit created. Candidate is not live-qualified.

## Scene-wait capture exclusion (branch scenewait-traces, kept offline, 2026-11-04)

- GI 1-ray's shot-rays-off-a failed on 1 timeout in 7 lines (max 26.483 ms).
  Float export stalls delay scene builds; these shots are not cost windows.
- Scope both GI timeout gates to its 4 cost windows. Shared frame-window preparation
  excludes trace_at phases from timeout gates and their wait_stats. Keep 0 timeouts
  strict in the remaining measurement windows; capacity and trace-count gates remain.
- Refresh affected active jobs, plans and combined manifests. Retain existing timings
  and corpus data. Shader requests, runtime settings and ABI are unchanged.
- queue_gates checks pass; live_flow 67/67 and batch_jobs_lint 7/7 pass, including
  all 31 active jobs against the frozen corpus and capture exclusion regressions.
- Fresh 74-log corpus lint: 11/31 active jobs and GI fail trace-schedule bounds.
  Read-only HEAD comparison of all 32 jobs finds identical failures, 0 new problems.
  Reports are in captures/.cache/scenewait-{active-lint,gi-lint,baseline-comparison}.json.
- No game launched, no GPU measurement, no Git metadata changes or commits.

## Scene-wait measurement exclusions (branch scenewait-traces, kept, 2026-11-04)

- ts1 OFF live 16:28: diag-off reported 59 current scenes, 1 timeout, max 16.688 ms.
  Diagnostic readbacks confound scene-build timing just as trace captures do.
- Exclude windows with traces or enabled *_stats, *_check or *_debug keys in
  pipeline_readiness.CONTROL, using cumulative settings. Keep zero timeouts strict
  elsewhere; capacity gates are unchanged.
- Regenerated scene-wait scopes and wait_stats in tileset OFF/ON, smoke_ultra and
  its active combined job/manifest. Preserve existing frame counts and timing evidence.
- Offline: queue_gates all checks passed; live_flow 68 tests; batch_jobs_lint 7 tests.
  No game launch, commit or generated corpus files. Next live check: ts1 cost windows
  still require 0 timeouts; diagnostic coverage/capacity gates remain mandatory.

## Far-GI screen-hit roof leak (branch roof-leak, offline fix, 2026-11-04)

- Confirmed: the screen-hit path accepted previous radiance without the hidden-hit roof segment test.
- Reuse `gi_far_roof_clear` for matched screen hits beyond the base GI radius. Blocked or unknown roof
  segments keep native ambient and stop before screen, emission, SHaRC or fallback radiance can supply the ray.
- No new key: existing `gi_far` specialization 1007 gates the correction. Near hits remain unchanged.
  ABI unchanged; `shaders/variants.txt` request delta 0; no preset changes.
- Offline regression: 2 tests, 12 segment fixtures pass; the original HEAD shader fails both tests.
  A 4 m canopy-crossing fixture with radiance 12 and radius 6 m changes bounce 6.666667 to 0;
  clear segments retain their original contribution. Covers rotated roofs, camera offset and unknown data.
- Full build passed; GI/roof/variant CTests 8/8 passed. One serial shader identity batch: 26/38 requests
  byte-identical; the 12 changed far-capable modules validate and all 12 match the baseline program with
  specialization 1007 frozen to 0 (other specializations preserved). No raw identity claim for those 12.
  Receipts: `build/roof-leak-{identity,off-proof,ctest,build}.log`, `build/roof-leak-off-proof.json`.
  Identity baseline cd534813d381; OFF baseline 3a60e2c0448a: shader/common trees are identical between them.
- No game launch. GPU cost and in-game image delta unmeasured; no live quality qualification claimed.
  Future check: S8 roofed interior with exterior ground on screen; inspect reduced leaked bounce and
  unchanged open-air bounce. This task does not authorize that live check.
- Kept as an uncommitted correctness fix for review. Git metadata untouched.

## Rejected controls retired (branch retire-rejected, kept, 2026-11-04)
- Removed five GUI controls; hair resolve and static-only cascade INI/runtime paths removed; three CPU keys remain hidden developer INI only, default 0 (no gain; hook submit +6.6 ms).
- Shader requests delta 0; 263/263 byte-identical to HEAD. Layer/core/settings/launcher build passed; 10/10 targeted CTests passed; no game launch or Git metadata writes.

## Release settings (branch release-settings, kept offline, 2026-11-04)
- 15 release controls; stale experiments neutralized, Full trace everywhere, Ultra-only OMM/masks; contract published; shader request delta 0 (no DXC needed).
- Settings 4,422 checks pass; bridge 20/909 assertions fail and JSON expects hidden Far + reflections (stream B); retired water settings removed, renderer references need owner cleanup; no live run.

## Release qualification jobs (branch release-qual, prepared, 2026-11-04)
- Release qualification jobs prepared: release Ultra S8, High OMM/mask candidate S8 (~7.0 min each), stock-hair Dialogue1 (~5.4 min); 15-minute caps, no game launch.
- Shared live_flow/lean_dryrun and batch_jobs_lint receipts are documented under tools/live_batches/release_qual; predictions retain exact source hashes and every-window gates.
- High dev=0 policy still vetoes the extension: rel2 explicitly bypasses it with dev=1; policy promotion/final confirmation and all live/visual evidence remain open.

## Stock-hair release packaging (branch release-package, offline checked, 2026-11-04)
- Final-control docs updated; wf9 qualified 08:59, Fresnel held, temporal promoted by user verdict. Package/install/diagnostics/lifecycle: 8/7/5/15 tests and 55 launcher checks pass.
- Full dry-run: 227 files, manifest diff 0; lean layer missing. No commit/live/install outside scratch/upload; owner decisions and final archive/VM/hardware acceptance remain open.

## Release licence payload and lean layer (branch release-pack2, kept, 2026-11-04)

- Package validates and ships LICENSE and LICENSE-EXCEPTION.md verbatim at its root.
  README states GPL-3.0-or-later with the NVIDIA SDK exception in two lines;
  THIRD_PARTY references both. User licence decision: 2026-11-04.
- Added scripts/build.ps1 -ReleaseLayer; packaging reuses it. Reconfigures build-nodiag
  explicitly with diagnostics OFF and RelWithDebInfo, then builds only the layer at BelowNormal.
  First build passed in 23 s. No game launch, upload, commit or Git metadata writes.
- Real dry run: 227 -> 229 files; missing inputs 1 -> 0; manifest differences 0.
  Receipt: .scratch/release-package/BG3RTX-audit-20261104-pack2/audit.json.
- Synthetic suites: package_licences 8/8, release_install 7/7, diagnostics_package 5/5 PASS.
  Tests use worktree scratch; real saves untouched. Shader request delta 0; ABI unchanged.
- Check offline with build.ps1 -ReleaseLayer and package.ps1 -DryRun -NoBuild -NoZip
  -Version <fresh-version> -OutDir <scratch>. Final archive and release acceptance remain open.

## Release launcher (branch release-launcher, offline verified, 2026-11-04)
- Kept: exact 15-control release contract, native/WebView pages, four presets/Custom, Help & support, focus/dependency/restart state; Advanced/Experimental dev-only; no F10 release copy.
- Launcher build and requested CTests 4/4 pass (bridge 977, settings 4,422 checks); no game or Git writes; manual zoom/DPI/high-contrast/Narrator and interactive parity remain open; shader request delta 0.

## Release raw migration and qualified exact-k (branch release-fix2, kept, 2026-11-04)

- Normalize raw `ls_shadowed=1` even when no complete choice matches: Fast -> 0,
  Full -> 3; preserve `ls_samples` and whether local shadows are enabled. Developer files stay intact.
- Audited all visible knobs with hidden raw choices: `local_shadows`, `box_lights`,
  `exp_hidden_surface`, `exp_native_coverage`. The other three already normalize raw keys.
  Tests cover partial box mode 1, Tilesets, reflection shade/GI and emission mode 2 across all presets.
- User decision: qualified exact-k k1 is automatic in Ultra with Full local shadows and RT geometry,
  with no release control; stale k/rest overrides are normalized. Lower presets remain Off:
  the approved Full-path evidence does not qualify them. Developer overrides remain available.
- Approved S7 inn: local shadows 0.717 -> 0.555 ms (-23%); pre-post scene energy
  -0.38/-0.34/+0.12%, within OFF spread 0.89%. Contract, TRACK and measured metadata updated.
- Schema regenerated; defaults check passed. Full MSVC build (vswhere/vcvars) passed.
  Settings: 4,672 checks; bridge: 987; pipeline async: 385. All four requested CTests passed,
  including launcher_web_bridge_json. Default shader identity: 251/251 identical to HEAD by input keys.
  Existing Ultra LS_K requests selected; variants catalogue delta 0; ABI unchanged.
- Check in game when convenient: S7 inn, Ultra, existing `ls exact-k: active` and `ls:` GPU lines.
  No game launched, queue used, Git metadata changed or commit made for this task.

## Temporal post in Ultra (branch promote-rr-post-temporal, kept, 2026-11-04)
- rr_post_temporal: Ultra On, Low/Medium/High Off; Lighting alongside RR; DLSS/RR and rr_post=1 required. User visual verdict: DoF much more stable.
- Dialogue3 VALID 032335: 0.152-0.153 ms; paired sign tests inconclusive (p 0.17-0.83). Check fine white hair over DoF in Test Dialogue3; shader request delta 0.

## Release backlog outcomes (branch outcomes, recorded, 2026-11-04)

- Water hit shading rejected for release; Experimental. wos1 16:08/17:01 compose refused every pass: light-binding 60/60, input-lifetime 57-60/60, binding 21-22 mismatch; diagnostic water_reflect shaded 2.37 M hidden hits/period. Fix 2f288a51 unchanged.
- Water hit cost bit 1 untested (depends on shading); bits 2/4 rejected, no saving.
- Pak publication rejected: camp 0% (22,686 unique HLOD_VT); S6 cp5 16:16 had 185,373 attempts, 16,616 unique, 0 published, all shader_pair (HLOD) rejects, missing UV 146,875.
- Generated rows rejected: camp 23.455% -> 23.314%, inn 23.671% -> 23.070% within OFF spread; GI +0.299/+0.527 ms. Earlier +2.3 pp not reproduced.
- Alpha spill rejected: as1 S8 15:13 OFF had no overflow, alpha made opaque 0.00.
- Checked existing decisions: three GPU image-gate rejections; Medium Half not shipped (0.25 ms); Fresnel Experimental; strands post-release; wf9 and High OMM/masks qualified; exact-k/temporal Ultra; GPL-3.0-or-later + NVIDIA exception; appearance settled: native ambient (S8 1.44), native S8 water shadow edge/glint and emission mode 1 kept.
- In progress: one-ray Ultra GI, Tileset, roof-leak fix cost (2aa9d0f3), hv9 hair visual. No promotion claimed.
- User check: retain selected release settings; rejected candidates need no new A/B. No game launch or new measurement; docs only, no Git metadata changed.

## OMM and offline masks enabled for High (branch omm-high, kept, 2026-11-04)
- High now enables exp_omm and exp_vt_alpha_masks with Full geometry and rt_alpha, like Ultra. rel2-smoke-high-s8 (12:13, VALID): 528331332 compared, 0 not compared, 0 violations, 13.9% fewer candidates, 0 scene-wait timeouts; all gates passed. Check High at S8; GPU ms not supplied. Schema/defaults checks passed; settings_test 4679/4679 and launcher_web_bridge_test 987/987; shader request delta 0. No game launch.

## Merge gates sized by changed paths (branch mg-scope, kept offline, 2026-11-04)

- merge-green classifies `git diff --name-only main...ref` before merging. Unknown paths,
  empty diffs and classifier failures keep the full gate. Renames include the deleted source.
- Docs: no build; no repository-wide Markdown/link checker currently exists. Tooling: changed
  Python/PowerShell/JSON syntax, changed job/plan lint with frozen evidence, shared queue and
  camera tests, plus affected registered offline Python CTests. No DXC, C++ build or QA.
- Shaders or common/shader_variant.h: C++ build, shader consumer CTests (including transitive
  local C++ includes), existing cached shader compilation and descriptor interface checks.
  Native/common/build or unclassified paths keep all CTests, shader compile and QA detectors.
- Game, USER_PLAYING, merge ownership/recovery and running-job guards remain in place.
  Test-tree checkout still updates; docs/tooling skip its redundant build. MERGED/NOT MERGED stay.
- Offline: tooling suite, affected Python CTests and PowerShell parsing passed in 39.5 s.
  Classifier/selection regressions cover mixed paths, renames and fail-closed gate errors.
- Planning estimates, not measured merge timings: docs < 0.2 min; tooling 0.2-2 min;
  shaders 3-10+ min; full 10+ min. Cold DXC/cache misses can exceed these estimates.
- No game launch, merge, commit or Git metadata write. Shader request delta: 0. ABI unchanged.
  Check the printed scope/reason and gate list in the next authorized merge-green run.

## Project licence (branch licence-gpl3, decided, 2026-11-04)
- User decision: GPL-3.0-or-later with a section 7 additional permission for the NVIDIA Streamline, DLSS, Reflex and Aftermath libraries (LICENSE-EXCEPTION.md). LICENSE is the official FSF text (SHA-256 3972dc97...6986).
- Still to do in the packaging stream: ship LICENSE and LICENSE-EXCEPTION.md, README licence section, THIRD_PARTY.md reference.

## Probe Aftermath (hv8-aftermath, READY_FOR_LIVE, 2026-11-04)
- Global device loss with startup aftermath=1 now polls for dump completion for up to 60 s before normal close; logs completion/path or timeout; remains a failure. Hv8 patterns retain the dump path.
- Offline: live_guard 21/21, runner synthetic completion/delayed-write/timeout/opt-out checks PASS; hv8 lint 0 failures (81 VALID logs; historical replay/readiness still unproven). No game launched; no commit.

## hv7 header/probe bisect (branch hv7-crumbs, OFFLINE_PREPARED, 2026-11-04)
- Replaced failed warm-order-cards with order-0 warm-all, then order-1 masks 0/8/15; two complete 300-frame periods each (92.6 s), probe-bit coverage gates and per-window fault breadcrumbs; estimate 7.1 min including load.
- live_flow, batch lint (6), shared-flow tests (61) and fake-queue dry-run pass; no game launch or commit. Runtime dependency: periodic crumbs currently use [hair primary], while this job requires [fault].

## Hair pass bisect (branch hair-bisect, prepared, 2026-11-04)
- hv7: Dialogue1 Ultra, masks 1/3/7, card-only warmup, two 300-frame coverage samples and swaps per window; crash/fail-fast results name the window. Shader request delta 0.
- Offline lint and participation/crash tests pass; fake queue paths pass. Six-minute dry-run budget fails: shared 9.72 FPS model gives 92.6 s/window, 318.9 s + 120 s load = 7.3 min. No game/real queue/commit.

## One-ray Ultra analysis (branch gi1-analyze, rejected for promotion, 2026-11-04)

- Reanalyzed `gi1-ultra-1ray.1.174818.log` without launching the game. All 16
  every-window gates pass. No settings, shader requests, ABI or Git metadata changed.
- Fixed two analyzer adapters: keep full-log exposure probes when selecting each
  trace; use queue_gates' reset-aware complete timing periods for short cost windows.
  Float pre-post means already exist in `dumps.json` (5c3fdabc, slot 0).
  No f32 preview or job change is required. Absolute capture paths work;
  `--captures-root` also resolves the float data.
- GPU trace medians, OFF-a / ON-a / OFF-b / ON-b: 6.735 / 4.710 / 9.216 /
  4.480 ms. Mean OFF 7.9755 ms; savings 3.2655 / 3.4955 ms (prediction 2.30 ms).
- Three-sample float luminance means in the same order: 0.192885549 /
  0.168029129 / 0.171767416 / 0.189523346. All exposures 0.000296308;
  normalized means 650.963015 / 567.075910 / 579.692131 / 639.616029.
- ON energy deltas versus mean OFF: -7.841622% / +3.947240%. OFF repeat
  spread 11.582592%; existing Student-t band +/-255.301394%, dof 1.
  Both energy gates pass formally; the large band provides weak evidence.
- Named metric: scripts.qa.detectors.temporal, mean temporal std excess in PQ
  code plus excess area per mille. Static OFF null areas 195.600 / 158.123;
  floor 195.600. ON-a versus OFF-a / OFF-b: excess 0.000636 / -0.000343,
  area 163.034 / 170.116: passes floor rule. ON-b: excess 0.003194 /
  0.002215, area 289.221 / 246.318: FAILS both comparisons (PQ limit 0.000978).
- Overall predicted.json verdict: INVALID, not qualified; static acceptance
  also FAILS. First pan (off-a) fails rigid registration: fewer than four
  consecutive aligned frames. No valid registered pan metric can be reported.
  Partial evidence is retained in the analyzer JSON instead of discarded.
- A future qualification needs all eight pan frames in all four states to pass
  the existing integer-translation registration and cross-window support checks,
  with measured displacement, plus both static ON repeats passing flicker.
  Use a rigid scene/control and compatible motion; unchanged held rotation may
  fail again. Keep three float/stat traces per state and reset-aware timing.
  No missing energy capture requires a rerun. No rerun was prepared or launched.
- Validation: six analyzer unit tests pass (full-log exposures, relocated paths,
  missing-exposure rejection, reset timing and temporal acceptance); diff clean.
- Detailed receipt: queue/logs/gi1-ultra-1ray.1.174818.fixed.gi1.json.

## One-ray GI Ultra qualification job (branch gi-1ray-qual, READY_FOR_LIVE, 2026-11-04)
- Prepared gi1-ultra-1ray outside watched inboxes. No game launch, git mutation or preset change.
- S8 is the worst evidenced one-ray flicker case: sh1/sh2 failed their OFF-repeat null;
  camp has higher trace cost but no comparative flicker receipt. Inspect shaded stone/roof transitions.
- Release Ultra, RR/FG 2, one launch: gi_rr_rays 2/1/2/1, static and held Q/E per state.
- Prior sh2: GI trace 4.31 -> 2.02 ms (-2.30 ms), float energy +0.16%; motion was missing.
- Gates: per-state GI trace ms/saving, float pre-post energy within existing OFF-repeat Student-t band,
  existing QA temporal detector/floor rule for both static and registered pan ON repeats.
- Every-window trace/burst participation, capacity, zero scene-wait timeouts; invalid registration fails closed.
- Budget: 225.9 s plan + 0.027 s expected readiness + 120 s load = 345.927 s; 1390 MB;
  actual launch cap 354 s. Shader request delta 0. Promotion is a separate settings change after live PASS.
- Offline PASS: live_flow 0 failing jobs, batch_jobs_lint 6/6, acceptance 4/4, QA detectors all passed;
  fake queue rejects missing evidence and accepts stripped structural job. No live quality pass claimed.

## Master RT hotkey (branch f9-master-toggle, kept, 2026-11-04)
- Ctrl+Shift+F9 always toggles all RT; Ctrl+Shift+F10 retains the saved Experimental flip. Both use ab_hotkey; queue ab_off is unchanged.
- Added master off gates rt_water_geometry, rt_water_reflections and rt_hair_groom; launcher/docs updated. Layer/launcher builds and settings/bridge/JSON CTests passed (3/3); no game launch or GPU cost measurement.

## Release decisions synchronized (branch docs-sync, kept, 2026-11-04)

- Docs only; aligned backlog, release plan, contract, offline status, handoff and progress.
- Stock hair ships; strands are a post-release Experimental update. Fresnel stays Experimental.
- One-ray Ultra GI and Tilesets remain in qualification; no promotion claimed.
- Closed stale wf9/High holds: wf9 17 gates, compose 0.154 ms; rel2 High VALID 12:13.
- Temporal Ultra accepted by visual verdict; retained failed energy/inconclusive statistical evidence.
- Recorded rejected candidates, no S8 overflow, licence/NVIDIA/static CRT choices and RTX 5090-only evidence.
- Manual launcher acceptance is DPI only. Removed spill run: prepared total 33.29 -> 27.46 min.
- Check: read current decision summaries and qualified receipts; no game run needed.
- Validation: documentation diff/whitespace and size checks. No code, shader requests or Git metadata changed.

## S6 catalogue startup (branch cp5-startup, READY_FOR_LIVE, 2026-11-04)

- Set rt_hit_material_paks=1 at startup so device creation loads the catalogue.
- Keep same-run OFF/ON toggles: pool_rollover refreshes paks_key from that same
  setting each frame; paks_rows gates publication with the startup capability.
- Regenerated cp5; 15 phases and 203.3 s after load remain unchanged. Current
  corpus refreshes trace timing metadata. No shader requests or ABI changes.
- Offline: live_flow preflight passes (1 job); batch_jobs_lint passes (6 tests).
- Fake queue dry-run passes both missing-evidence rejection and structural runs.
- Live check: require catalogue load of 40,983 candidates and positive witnessed
  publication/verified basis in both ON diagnostics at Adamantine Forge (S6).
- No game launched. Publication, image/energy and GPU cost remain unqualified.

## Cold runtime cache readiness (branch cold-cache, kept offline, 2026-11-04)
- Chose (b): runtime COM DXC identity/debug keys and store differ from offline CLI cache; renew only pending prefetch inactivity on new completed builds, retain 15-minute launch cap. Requests delta 0.
- wf9 replay: 27 warmup completions; inactivity 11.945 s at old 180 s timeout. Camera checks 661, live-flow 61 and batch lint 6 pass; no game launch or commit.

## Groom TLAS labelled apart in fault dumps (branch claude/hair-tlas-label, READY_FOR_LIVE, 2026-11-04)

- hv7 08:40: the breadcrumb note of the TLAS the probe traced mapped to a "split TLAS slot" range; ensure_tlas gave the
  groom TLAS of a slot the same label as the scene TLAS. Now "hair groom TLAS slot".

## Order-mode prepass swap engages (branch claude/hair-prepass-swap, READY_FOR_LIVE, 2026-11-04)

- hv7 09:31: 0 prepass strand draws next to 187 G-buffer ones. The prepass required g_applied_frame == now; the game
  records the next frame's prepass before the present that advances frame_index, so the last G-buffer apply carries
  this frame's number or the next. Accepted both. New counter on the 300-frame line: prepass card depth dropped
  without its G-buffer swap (the hair would miss there: predicted 0).

## Strand coverage from the pixel record, two-sided bound (branch claude/hair-pixel-coverage, READY_FOR_LIVE, 2026-11-04)

- hv7 09:31: strand/card 2.55 on the close-up: the strand occlusion query (masked probe, swapped variant) counted every
  overlapping card layer's fragment; the prepass card query (depth writes) far fewer. Not a coverage measure.
- Now the strand side is the pixel record: unique strand pixels per groom of the counted G-buffer frame (one entry per
  pixel), mapped to the draw key of the header that frame applied, counted with that frame's tag (the counter had used
  a newer publish's tag). The G-buffer strand queries are removed; the probe still draws (it writes the entries).
- Bound two-sided: [0.891, 1.094] (offline min 0.911 / max 1.075 over 2631 views, minus / plus the modelled noise).
  Gates: queue_gates items.upper; hv6 coverage gate; hv7 prepass count any (the prepass swap follow-up).
- Predicted on the Dialogue1 close-up: 0.80-1.05 (the prepass card samples may still count a few stacked layers: a
  ratio just under 0.891 then keeps cards, fail closed). Far LOD2 draws: low ratios, cards stay.

## Coverage pairing with history, keyed by the prepass frame (branch claude/hair-pairing, READY_FOR_LIVE, 2026-11-04)

- hv7 10:25: the count found strand pixels (evidence from tag - 1 / - 2) but every draw read 0/0: card and strand
  samples were kept newest-only per key and arrived with different latencies, so their frames never met.
- Now hair_primary::CoveragePairing keeps 16 frames per key and side; the G-buffer frame records the prepass slot
  recorded before it (card_frame) and the strand pixels pair on it. Evidence age bound: 12 frames (card readback 4 +
  strand count up to 3 tags behind + 3 publishes + 2). The strand-traces line logs G-buffer frame, card frame, keys and
  pairs. Unit test on the real ordering.

## Hair coverage: depth latch epoch, alpha-tested card pixels (branch claude-hair-orphan-alpha, READY_FOR_LIVE, 2026-11-04)
- Depth obligations now span a prepass epoch, independent of present/frame numbering.
- Only a G-buffer pass permits the next epoch; sibling fallback and failed-key handling are retained.
- Unit regressions replay prepass F / G-buffer F+1 and both at F: refusal takes fallback, zero orphans.
- Repeated prepasses retain obligations; a genuinely unresolved epoch counts one orphan once.
- Card markers require the native combined-mask alpha test at card UV, with grazing and existing dither.
- Marker placement remains before strand rejection; strand shading and acceptance are unchanged.
- Shader request delta: 0; only rt/hair_primary_gbuffer.hlsl changed, no ordinary shader/include edits.
- Validation: build.ps1 -Test passed; all 149 CTests passed (334.49 s), including both recording orders.
- G-buffer and count shader requests compiled and passed spirv-val: 2/2, zero failures.
- The first suite hit an unwritable shared shader cache; rerun passed with BG3RTX_SHADER_CACHE=build/shader_cache.
- Live quality and GPU cost unmeasured. No game launch, commit or git metadata modification.
- In the next authorized hv7 close-up A/B, check fallback/orphans and per-key accepted/card coverage.

## Hair depth latch: late G-buffer resolution (branch claude-hair-latch-late, READY_FOR_LIVE, 2026-11-04)
- hv7 evidence: 900 prepass, 901 G-buffer, 900 velocity swaps; 0 fallbacks, but 19-50 premature orphans per 300 frames.
- Retain current and previous depth epochs; only unresolved old previous keys expire.
- Resolve both epochs before rotation; retain deferred success/failure bookkeeping for sibling-draw fallback safety.
- Log first 4 true orphans per period with key prefix and original prepass/drop frame_index.
- Regression covers interleaved prepass/G-buffer recording, late fallback eligibility and one missing-swap orphan.
- Existing recording-order tests retain their checks with orphan expiry delayed by one epoch.
- Validation: powershell -NoProfile -File scripts\build.ps1 -Test passed; 149/149 CTests, 146.93 s.
- First run: 148/149; shared cache SQLite unavailable. Rerun passed with BG3RTX_SHADER_CACHE in this worktree.
- No shader or ABI changes; shaders/variants.txt request delta 0. No game launch or GPU cost measurement.
- Live check pending: hv7 same-run participation, fallback and orphan counters; inspect hair depth in Test Dialogue1.

## Strand variants return after every discard (branch claude/hair-demote-fix, READY_FOR_LIVE, 2026-11-04)

- hv8 Aftermath: the page fault (VA 0x2564a00000, far past the mesh pool) is in rt/hair_primary_gbuffer.hlsl (the
  coverage probe instance), SPIR-V line 1352: the strand points load right after OpDemoteToHelperInvocation. DXC lowers
  discard to demote; a demoted lane keeps executing, so after hp_segment failed (i0 garbage from the segment table) the
  points load still ran. Same class as the hc1/hv5/hv6/hv7 losses (all reads far past the pool, all once rays hit).
- Fix: in the strand G-buffer and velocity variants every discard returns at once; the card UV derivatives move before
  the first return. Live check: hv7 probe-only and all-passes windows without device loss.

## Strand probe gate and breadcrumbs (branch claude/hair-crumbs, READY_FOR_LIVE, 2026-11-04)

- hv7 08:10: device lost about 2 s after order 1 with rt_hair_primary_passes 0: the coverage probe (the masked strand
  G-buffer variant, now hitting) was still drawn. Bit 8 of rt_hair_primary_passes gates the probe (default 15).
- Breadcrumbs: the strand G-buffer variant checks its explicit buffer accesses (pixel entry, points after hp_segment,
  point UVs) against their ranges, records a violation (site, address) in a host-visible buffer of the layer and skips
  the access; it also notes the last TLAS, pool and pixel addresses. Logged every 300 frames and after a device loss
  ("hair primary breadcrumbs: ..."). No violation with a device loss: the fault is in the TLAS traversal or a
  descriptor access, not in an explicit read.
- The access violation after hv7's device loss is the game's (bg3.exe write at 0); the layer frame is our exception
  logger (streamline.cpp veh).

## Hair coverage: accepted strand pixels, unique card pixels (branch claude-hair-coverage-accept, READY_FOR_LIVE, 2026-11-04)
- P0: replaced stacked prepass occlusion samples with unique card-pixel tags in the G-buffer shader.
  Probe and swapped draws use the same marker planes; removed card queries, precise-query requirement,
  private probe header, cross-pass CoveragePairing and native-query tracking hooks.
- Draw attribution: native FS inputs have no index-range key, and misses cannot identify a groom.
  Specialization 1102 supplies a compact draw-key slot (16 total), stable while live; a staged
  groom-to-slot map groups shared keys. Overflow keeps cards/no probe/no swap; coverage logs
  `draws without a slot N`. shaders/variants.txt request delta: 0.
- P1: accepted marker is written only after groom, segment, ownership, UV and tip rejections.
  The sealed ray cache remains reusable; refused/other/traced diagnostics keep their original rules.
- P2: duplicate applied tags immediately invalidate evidence; ambiguous readback is rejected and logged.
- P3: latest out-of-bounds evidence blocks later swaps; delayed older successes cannot replace it.
  Log distinguishes latest ratio from min/max used for actual swaps in every pass; untruncated
  per-key line: `[hair primary] coverage used %016llx: ratio %.6f..%.6f; %u swap decisions`.
  Supplied hv7 period totals cannot reconstruct the evidence used for its four eab0 swaps.
- P4: frame/key depth latch chooses native GREATER_OR_EQUAL + depth-write fallback when strands refuse,
  also for supported native fallback pipelines whose strand clone refused. Sibling draws retain the latch.
  Unresolved keys, including absent draws or unavailable native fallback, remain true orphan diagnostics.
- CPU regressions replay prepass -> G-buffer -> delayed readback; cover ambiguity, bounds exit,
  old success, native fallback/zero supported-path orphans, stacking, rejection and overlapping keys.
- One packed card/accepted pair (8 B/pixel) plus 16 B cache: width * height * 24 B,
  65.6 MB / 62.5 MiB at 2560 x 1067, independent of groom count (formerly 208.4 MiB at 8).
  Pipelines per supported native G-buffer: 16 strand + 16 probe, formerly 64 + 64.
  Unswapped prepass depth gives one front-layer owner; swapped overlaps use last writer for both words.
  Count masks tags to 24 bits and requires equal packed words for acceptance. Named offsets asserted.
  CPU tests cover stable/reused slots, 17th-key overflow, four tags, overlap, rejection and tag wrap.
  Allocation beyond 32-bit count offsets keeps cards. GPU cost remains unmeasured.
- Targeted helper: 3/3 requests compiled and spirv-val passed (2 rebuilt, velocity cached).
  No extra variants.txt requests exist for these files. No shared shader include changed; ordinary
  shader source closures are unchanged, so no full identity run was requested.
- Full build/CTest: passed, 149/149 CTests (121.24 s), including hair GPU and host_atomics_lint.
  BG3RTX_SHADER_CACHE points to this worktree's build/shader_cache; external shared cache is unwritable.
- Live: not run. Keep rt_hair_groom_primary off in presets; kCoverMin/kCoverMax and ABI unchanged.
  On Dialogue1 close-up, require every-window ratios 0.891..1.094, used ratios inside bounds, zero
  orphan depth drops, visible complete hair, and GPU cost; then smoke_ultra. No queue files were touched.

## Coverage count reads the newest tag present (branch claude/hair-count-tags, READY_FOR_LIVE, 2026-11-04)

- hv7 09:48/09:57: strand pixels 0 for every draw although rays hit: the count (recorded after the G-buffer pass) ran
  on the GPU before the last G-buffer passes; the record held tag - 2 while the count looked for tag.
- The count shader now counts strand pixels per groom for the four newest tags; the layer takes the newest with
  entries and its G-buffer frame's keys (per-frame applied tag record). The strand-traces line logs the four totals.

## Strand shadow crossings: shared-joint dedup, RGB transmittance, fibre expectation (branch claude/hair-core-shadow, READY_FOR_LIVE, 2026-11-04)

- Review core1: chained segments merge into one crossing only when they share the joint (segment start table in
  InstInfo word 3, rt_groom_links); the last segment of a strand and the first of the next no longer merge.
- core3: rt_groom_blocks multiplies the RGB transmittance over the crossings and decides once on its luminance.
- core4: with rt_hair_groom_shadow_t and rt_hair_fibre, the fibre pass subtracts D-LS's expected strand transmission
  d_f lum(a_f^n) instead of 0 (exact for the pixel's own groom).
- Identity vs main f7e5ea44: 149/257 byte-identical, 107 equivalent at default (1026 = 0), the last failure was main's
  velocity change (identical after merging main); request delta 0. GPU shadow bench in hair_primary_gpu_test.

## Fault dump lists the cascade and groom BLAS as live; strand pass bisect key (branch claude/hair-blas-lifetime, READY_FOR_LIVE, 2026-11-04)

- hv6 07:30 lost the device 2.5 s after the first real strand swaps. The dump's "#552 (RELEASED range)" was the current
  cascade casters BLAS (the last instance with rt_cascade_casters): the dump's live set left out g_casc_as and the groom
  BLAS. Fixed; BLAS lifetimes were not at fault. The invalid read (0x25be68a000) is in no buffer of ours: unresolved.
- rt_hair_primary_passes (Developer, live, default 7: 1 G-buffer, 2 velocity, 4 prepass clone) bisects which strand
  pass faults once draws swap. Live check: hv6 order-gbuffer windows with 1, then 3, then 7.

## Non-hair release backlog jobs (branch backlog-jobs, prepared, 2026-11-04)
- Repaired shading A/B and split water cost bits 1/2/4; prepared S6 pak publication, generated rows and GI guard rejection screens.
- S8 spill witness: 0.61 substituted alpha draws/frame, zero opaque loss; Current Sun tests readiness and real ray participation.
- All requested jobs have predictions, finite cold waits, offline lint and synthetic dryruns; no live qualification or launch authorized.
- SHaRC uses an offline PT/history plan; missing matched history rejects release inclusion. GI PNG screens cannot promote transient guards.
- Run order and validation limitations: tools/live_batches/backlog-jobs/README.md; no Git metadata writes or commits.

## Current qualification receipts

- wf9 VALID 08:59: 17 gates, static unchanged, turning hits about 6.7% -> 17%;
  compose 0.154 ms absolute. No release blocker remains for wf9.
- rel2 High VALID 12:13 qualifies OMM/masks; rel1 Ultra VALID 13:47;
  rel3 stock hair VALID 13:51. Review retained stock-hair images separately.
- Exact-k Ultra accepted: S7 0.717 -> 0.555 ms (-23%); energy within OFF spread 0.89%.
- Strands: demote fault fixed; hv7 VALID 12:37 swaps on 2 draws. Close-up 0.80 < 0.891;
  coverage/orphan follow-up remains. This supersedes the morning first-swap blocker.
- Packaging/provenance, DPI, memory/transition and Act 2/3 acceptance remain open.
  Repeat integrated smoke only for a material release change. New lives need explicit steering.

## Earlier night evidence (historical)

## Night report, part 2 (04:00-07:45)

- Bundles 16-22 plus four tooling merges; main de413e94. ABI unchanged (26).
- Water Fresnel: wsf1 VALID 06:44 (281 s). Pool rim +43% ON, open water +1.8%, surface FS 0.10-0.15 ms;
  stays Experimental by user decision. [Result](../tools/live_batches/water_surface_fresnel/RESULT_wsf1_064436.md).
- Hair strands, found and fixed in sequence by hv6 lives: recursive scene lock (load exit), coverage queries
  never paired (0/0), probe at the card's own depth, rays mirrored vertically (clip y not negated, 0.66 m off).
- Hair now: strand rays hit; hv6 07:30 lost the device 2.5 s after the first real swap. The fault dump shows
  in-flight scene TLAS referencing a released BLAS (#552). Later hv7 supersedes this fault status; see current receipts.
- Hair core 1/3/4 (shadow crossings): identity 147/257 byte-identical, 107 equivalent, 3 main-side differences;
  cached recheck running on claude/hair-core-shadow.
- Tooling fixed from INVALID runs: readiness waits a full producer period (wsf1 2.0 -> 6.4 s), trace capture
  stall budgeted (VALID p95 2.72 s, wsf1 windows 12.0 -> 15.4 s), bursts arm after readiness, stale prefetch
  requests dropped, lint no longer double-counts settings latency (active 26/26).
- Hair job gates: strand gates required only when a coverage period reports swaps, plus a job-level gate that
  card and strand samples are both nonzero (no vacuous VALID).
- No crash dump this part; one device loss (hair, above). Game closed after each run.

## Night report, part 1 (to 04:00)

- Bundles 12-15 merged; smoke_ultra VALID twice (cascade buffer full 0.0, OMM 1.23 G rays, 0 violations).
- Camp pak join: all 22,686 unique candidates Engine_Default_HLOD_VT; legitimate publication 0%. Needs S6.
- Hair conversion 877/1,217; all companions except Gale beard LOD1. Water glint (M5.6 step 1) rejected offline.

## Evidence boundaries

- [Night archive](archive/PROGRESS_20261104_night.md) keeps the earlier 495-line file; branch details are in git
  history (fragments folded at de413e94) and in each TRACK doc.
- [Handoff](HANDOFF_SESSION.md) gives the next queue; [GOAL_NEXT](GOAL_NEXT.md) defines stages 2-7.


## Stage 2 - Shader specialization: done

- Migration merged; scc1 VALID, 0 DXC compiles on converted toggles.
- Requests 2,946 -> 147 -> 135 before later layouts. These are historical snapshots, not today's total.
- Bundle 6 gate: 262/262 requests, 28 hit-shade interfaces, 0 failures; full-resolution identity 243/243.
- New on/off keys use specialization constants unless layouts/bindings change. Retired IDs stay reserved.
- Open: first-use pipeline continuity/toggle latency. Readiness repairs do not prove rendering quality.

## Stage 3 - Visible defects: qualified fixes kept

- Dialogue cascade identity matches 16-18 duplicates/frame, formerly 0; moving band fixed.
- AO self-occlusion is accepted: 50% of character pixels; 99% of self-hits below 5 cm.
- wf9 is qualified; see current receipts. Reflection edge/hit-distance keys remain Off.
- Removed null keys: rt_water_rr_mv, rt_water_fg_mv, rt_water_glint_aa.

## Stage 4 - Qualification: stock release; strands post-release

- Ultra qualifies rt_omm, rt_omm_dyn, rt_vt_alpha_masks, rt_alpha_fine_lod and ls_box=2.
- ls_box_honour_shadow=1. Two later smoke_ultra runs are reported VALID with zero cascade overflow.
- Earlier OMM camp: 1.17 G rays, 0 violations; RT 8.65-8.75 vs 9.37-10.08 ms. Later smoke: 1.23 G rays.
- Box S6: signed energy delta 0.0 within noise; unshadowed traces 369 M -> 0; local lights +0.13 ms.
- Pool alpha S6 coverage 14.8% -> 52.1%; GI trace +0.46 ms. Wind RGB S8 45.0% -> 63.5%; +0.67 ms.
- Mask rows: S8 +0.65 coverage points/+0.006 ms; inn +12.1 points/+0.92 ms.
- Earlier secondary grooms cost about 40% less than native cards. This does not qualify primary strands.
- Primary strands remain Off in presets; post-release coverage and GPU safety remain unqualified.
- rr_post_temporal is accepted for Ultra by visual verdict. Earlier paired evidence has 10 pairs.
- Exact p range 0.171875-0.828125 vs 0.00416667; presented registration rejects 9/20, face support 0.
- Paired float energy +0.1492% fails; background join fails. Cost 0.152-0.153 ms; no statistical quality pass.
- User sees steadier DoF and accepted promotion; this does not change the failed/inconclusive gates.
- Rejected: CPU perf keys, rr_hair_resolve, hair GI culling (+9-20% energy). No new queue for these.
- IP grass stays off: +3 coverage points costs +1.9 ms and about +1.5 GB.
- Generated-row publication/recovery remains unqualified. GI guards are rejected.

## Stage 5 - Effects and performance: retained evidence

- Ultra keeps water + Refraction, hidden-surface shading + Bounce, rt_hit_emission=1 and gi_far=6 m.
- gi_far: S8 energy -2.01%, camp -2.42%; trace +3.2/+3.7 ms. One-ray Ultra GI is now being qualified; no promotion yet.
- Fresnel uses native foam coverage c plus clean BRDF (1-c), with finite/domain/partition checks.
- wsf1 VALID 06:44: rim +43%, open water +1.8%, FS 0.10-0.15 ms, compose 0.61-0.67 ms; no PT reference here,
  it remains Experimental by user decision.
- M5.6 step 1 water glint is reported rejected offline; retain evidence, do not queue it.
- Camp pak join proves 0 additional publication: all 22,686 unique observations fail recipe/cull.
- Unique observations are 5.655% of attempts and 3.762% of draws. Raw UV matches alone cannot publish.
- Need a compatible Tileset/HLOD-mask scene, e.g. S6, with full witness/resource/recipe/cull/basis gates.
- Compact v4: 40,983 rows/40.01 MiB. Native precedence remains mandatory; rt_hit_material_paks stays off.
- Water sparse-hit reflection 2.107 -> 0.530 ms, repeat 2.491 -> 0.531 ms; energy/coverage not qualified.
- wo1 must prove post-water float energy within OFF noise, preserved coverage and zero truncation in every ON.
- rt_compact_hit_state, rt_alpha_metadata_cache and rt_water_empty_tiles are rejected/Off.
- Offline replay is 36/36 byte-exact; empty-tile compose saving ~0.156 ms is exploratory only.
- Full tracing everywhere. Medium Half rejected: about 0.25 ms saving without quality acceptance.
- SHaRC is rejected unless matched inputs exist. Hit-GI AO needs instanced-hit inputs.
- Animated lava, HeightGradient, HLOD sidedness, texture publication and AS fragmentation remain open.

## Stage 6 - Tooling and release: repairs kept; acceptance open

- Fail-fast, preflight lint, 15-minute launch cap, measured-FPS windows and session fault invalidation are in place.
- Readiness handles cached/no-op keys and hotkey keys; every new affected build still needs exact completion.
- Five cached-wait replays pass: hv6 25 ms, hv5 79 ms, wsf1 66 ms, cp4 86 ms, Dialogue3 57 ms.
- Frozen lint corpus: 223 KB derived from 90 VALID runs; preflight still uses live corpus.
- Folded checks: 147/147 CTests, 26/26 active lints, 6/6 combined dryruns. These are offline receipts.
- REFUSED/CRASH rows and crash monitor are reported added; verify runner behaviour on real use without fault injection.
- GUI fixes cover 13 findings; settings 4,356 checks and bridge 904 passed. Manual check is DPI only.
- Version-matched DXC/Vulkan notices are resolved: five texts retained verbatim; packaging tests 3/3 pass.
- Licence, notification and static CRT choices are settled above; game-data rights remain open.
- Installer, memory/transition and Act 2/3 remain open. Only RTX 5090 tested; no release approval implied.

## Stage 7 - Backlog and cleanup

- Follow the current release handoff; strand fixes belong to the post-release update.
- Open branch: water-glint, evidence only. Earlier open-branch lists in the archive are historical.
- Audit rejected keys/generated-row supersession, inn material records, BLAS relocation and M5.5 steps 2-4.
- Keep current replay/gate inputs; remove worktrees only with the dedicated junction-safe script.

## Post-release RTX hair

- [Hair plan](RTX_HAIR_COMPLETE_PLAN.md) retains M0-M8 scope and earlier evidence.
- 877/1,217 conversions pass; Gale beard LOD1 remains open.
- Shadow crossings 3,000/3,000; tips exact at 1,600 samples/alpha; fibre furnace 1.0.
- These offline results do not qualify coverage, temporal quality, cost/VRAM or GPU safety.
