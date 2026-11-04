# BG3RTX progress - 2026-11-04 (main ab66c855 + rc-final)

## Report: backlog closed, release candidate ready for approval

**Question for the user (only one):** approve release candidate rc-20261104-ccbbb1d2-0efe5f84?
Nothing is uploaded. The archive is `.scratch/rc/20261104-182143-0efe5f84/BG3RTX-rc-20261104-ccbbb1d2-0efe5f84.zip`
(72.60 MiB, SHA256 29f1fa9a...8446f, not in Git).

**Done.**
- Every release-backlog row has a final outcome with numbers ([backlog](BACKLOG_20261104.md)).
- Release check PASS ([RC_CHECK](release/RC_CHECK.md)): lean layer without diagnostics, 229 files, delta 0,
  hashes and licences checked, install, update and uninstall on a throwaway folder.
- Final Ultra smoke: rel1 17:52 VALID on the release profile. Later merges (bundle34) touch only tools and docs,
  so no further smoke is needed.

**Shipped in the release profile.**
- Ultra: temporal post (0.152-0.153 ms, user visual verdict), exact-k (S7 0.717 -> 0.555 ms), two-ray GI.
- High: OMM and offline masks (rel2 VALID 12:13).
- Always on: water camera correction wf9 (turning screen hits 6.7% -> 17%, compose 0.154 ms),
  far-GI roof-leak fix (rel1: GI 11.46 ms vs 11.97 ms before, no measurable cost).
- Appearance: native ambient (S8 ratio 1.44), native S8 water shadow edge and glint, hit emission mode 1.
- Ctrl+Shift+F9 master RT toggle; F10 flips the last Experimental choice.
- Licence GPL-3.0-or-later with NVIDIA section 7 exception. "Tested only on RTX 5090".

**Experimental (off in presets).** Water Fresnel (rim +43%, open water +1.8%, invisible to the user on open water).

**Rejected or deferred today, with numbers.**
- One-ray Ultra GI: saves 3.27/3.50 ms GI trace; energy -7.8%/+3.9% inside OFF spread 11.6%; static flicker
  fails in ON-b (PQ excess 0.0032/0.0022 > 0.00098). Rejected for Ultra.
- Native Tilesets: OFF VALID (RT 9.92 ms, GI 12.03 ms); ON INVALID twice for test reasons (no rt_debug=9,
  then the pipeline still building in the 60-frame diag window). Hidden and off; post-release.
- Water off-screen hit shading: compose refused every pass (light binding 21-22). Bits 1/2/4/7 rejected.
- Pak publication: 16,616 unique S6 candidates, 0 published. Generated rows: no coverage gain, +0.3/+0.5 ms.
- Alpha spill (no overflow), three GPU candidates, Medium half resolution, glint, GI guards, current sun,
  IP grass, SHaRC.
- Hair strands: post-release. hv7 16:06 VALID (orphans 0, close-up 0.97); hv9 18:02 fail-fast on 3 orphan
  card depths in one frame. Rule: no hair lives until consolidated offline (review found 9 issues,
  all hair-only, gated by rt_hair_groom).

**Open, not claimed.** Fresh-PC install, signing, Inno installer, Act 2/3 acceptance (the user tests these),
upload.

## Current user decisions

- Stock visible hair ships. Strands are a post-release update on the Experimental page.
- Fresnel Experimental; native ambient and water edge/glint; emission mode 1.
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification.
- The asset database derived from the game (8 MB) ships in the archive, at the user's own risk.
- Static CRT; no VC runtime ships. Only RTX 5090 tested. Launcher acceptance is DPI only.
- Lean: live windows in frames, one launch per scene, merge-green scaled to changed paths.
- RTX Hair: consolidate and verify offline first; one approved READY_FOR_LIVE job afterwards.

## Release UI/UX review: launcher, settings texts, installer, player docs (branch cons-ui, tests PASS, 2026-11-05)

- 26 changes, offline only. No game launch. CTest 167/167 pass (settings, schema, bridge, launcher UI).
- Experimental shows only Water surface reflections in this release (`kOptInKnobs`); strand hair returns
  with its post-release update. Before: 8 unusable "unqualified" hair cards; one could be turned on.
- Screenshot mode now renders the real settings table (Ultra, reference RTX 5090, no player files read)
  instead of a stale JavaScript sample. `--screenshot-scale` and `--screenshot-bottom` now work in the WebView.
  New views: surfaces, performance, needs, preset.
- Needs a layer rebuild because settings.h changed: rerun verify_rc before the release.

| Page | Before | After | Why |
|---|---|---|---|
| Experimental | 8 strand hair cards and Fresnel, "unqualified" wording | Water surface reflections only | Release facts; no empty promises |
| Experimental | "Water interface Fresnel", "On (unqualified)" | "Water surface reflections", On; plain help, restart stated | Plain wording |
| All cards | Badge showed only the current choice ("Applies after saving" on water Off) | "May need a restart" when another choice needs one; tooltip lists them | Restart state was hidden |
| Lighting | Awkward help for Ray-tracing detail, Sun shadows, Indirect lighting | Plain one-line help; two rays named | Clarity |
| Lighting | Off-screen bounce and hair shadows had no cost | Cost lines from recorded RTX 5090 numbers | Report cost of heavy effects |
| Lighting/Surfaces | Details said "Requires..."; DoF said "paired tests did not establish an improvement" | "Needs ...", restart rules, where to look | Consistent, truthful |
| Surfaces | Water had no cost or restart detail | Estimate 0.5-0.7 ms, 520 MB, restart rules | Cost and restart |
| Performance | Order FG, limit, Reflex; vague FG details | FG, Reflex, limit; on/off restart, 2x/3x/4x live | Dependency order, restart truth |
| Header | "GPU not detected · Not detected" | No duplicate | Duplicate text |
| Header | Preset tooltip: suggestion only | Preset description plus suggestion | Explain presets |
| Preset review | "58 -> 0", no restart marks, no summary | "58 fps → Off", "(after a game restart)", preset description | Readable, restart state |
| Help & support | Video memory "32 GiB" twice | Once | Duplicate |
| Help & support | No "tested only on RTX 5090" | Compatibility says it | Release fact |
| Help & support | Several GPUs: "Graphics check failed" | "Several GPUs found. Choose the one the game uses" | Wrong message |
| Help & support | Comparison/overlay text | Ctrl+Shift+F9 explained; overlay advice for start/FG failures | Shortcut explained |
| First run | Missing game folder only in the footer | Notice on every page with "Choose game folder…" | Play was disabled without a clear reason |
| Native fallback | "(awaiting qualification)", "(experimental)" | "(Experimental)"; same help texts | No internal wording |
| CLI | "recheck in Overview" (no such page) | "recheck in Help & support" | Stale |
| Installer | One-line welcome; claims nothing about hardware | Welcome: what it installs, Vulkan + DLSS, tested only on RTX 5090 | Clear steps |
| Installer | Default finish page; errors pointed to SETUP.md (not shipped) | Finish text names the next step; errors say run setup again or report | Clear steps, useful errors |
| USER_SETTINGS.md | Rows out of page order; "Disable other Vulkan overlays in Help & support" (removed); F10 claim | Page order, Experimental row, Ctrl+Shift+F10 truth (no eligible option yet) | Docs match UI |
| Release README | "Browse...", removed overlay checkbox, "Ultra" OMM only, jargon cost table, stale smoke line | Current names, High+Ultra OMM, plain cost table, RTX 5090 note, Ctrl+Shift+F10 truth | Docs match release |

Screenshot pairs (`.scratch/ui-review/before|after/`, untracked; 100/125/150/200%, and 720-wide):
lighting, lighting-bottom, surfaces, performance, support, support-bottom, experimental (all four scales);
needs, preset, report, missing, probe, error, restart (100/200%); advanced-1; lighting/experimental/performance-1-736.
Layout: identical at all scales (the window keeps its logical size), no clipped text or wrapped buttons found.

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

## Windows release installer (branch inno-setup, blocked qualification, 2026-11-04)

- Added `build_setup.ps1`: VERSION.txt version, external payload.sha256.json with 229
  hashes, combined GPL/exception page, pinned Inno 6.7.3. Outputs only under .scratch/.
- Setup keeps the fixed AppId and transactional lifecycle; per-user, original icon,
  Start menu, unchecked desktop option, finish launch, Vulkan + DLSS welcome text.
- Upgrade keeps settings. Uninstall defaults to keeping settings, supports explicit
  /REMOVESETTINGS=1, removes shader cache, and delegates shortcuts/Apps cleanup to Inno.
- Added actual Setup RC stage: explicit throwaway /DIR, shortcuts disabled, isolated
  LOCALAPPDATA/TEMP, 229 install/update hashes, cache/settings keep/remove and Apps checks.
  Refuses existing Apps registration; retains recovery artifacts if setup cleanup fails.
- Fixed fresh-worktree packaging: use the lean build's manifest and production Streamline
  copies. Payload remains 229 files, 0 missing inputs and 0 manifest differences.
- Full verify_rc rerun 18:42:58-18:45:12: 11/12 rows PASS, Setup FAIL before compilation:
  ISCC.exe execution denied by Windows (Access is denied). No setup EXE was produced or
  executed; no Apps entry was created. ZIP 72.61 MiB; 226 source hashes, 12 DLLs,
  9 production Streamline DLLs and 15 notices PASS; install/update 229 hashes each;
  uninstall restored 5/5 originals; fake game unchanged; throwaway TEMP tree removed.
- Tests: lifecycle 15/15 (75.1 s), release_install 7/7 (15.0 s), setup metadata 4/4
  (1.9 s; also CTest PASS), package licences 8/8 (20.1 s): 34/34 PASS.
- No shader requests added (delta 0); no rendering/ABI changes or game launch.
- Commit blocked: git add cannot create .git/worktrees/inno-setup/index.lock (Permission
  denied); required git merge main similarly denied at ORIG_HEAD.lock. Changes are unstaged.
- Remaining: permit compiler execution and Git metadata writes, rerun verify_rc, commit
  with the requested Codex trailer. Signing/fresh-PC/interactive wizard acceptance remain open.
- User check after qualification: run Setup in a disposable user account, inspect icons,
  licence page and shortcut options; update retains settings; Apps uninstall defaults to keep.

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

## Shader consolidation fixes (branch cons-shaders, offline verified, 2026-11-04)

- F04 confirmed/fixed: retain the original product/root for finite nonzero products; log2/exp2 fallback for overflow/underflow, with explicit zero handling. Fifteen 1000 samples: black -> 1000; fifteen 1e-6 samples: zero -> 1e-6. 640 ordinary fixtures exactly unchanged.
- F06 confirmed/fixed: clamp the descriptor mip before computing page coordinates. Mip 4 through a three-level view at u=.75/dim=64: wrong mip-2 page 3 -> page 12; available mip cases unchanged.
- F07 confirmed/fixed: screen GI trace/contact reject UV >= 1 before loads. UV 0 and interior cases unchanged; both upper-bound axes reject.
- C10 confirmed/fixed: validate finite float coordinates and bounds before integer conversion; return explicit lookup validity instead of reusing the current pixel. Includes finite but out-of-int-range vectors.
- C10 callers checked: screen GI preserves native ambient; RT GI uses its existing missing-source/hit-light policy; reflections retain probe/fresh native shading; native_hit_gi_screen returns failure. The separate temporal accumulator rejects invalid/outside positions and selects fresh cur directly, never a black history substitute.
- F03 confirmed/fixed: finite sums are clamped to [0, 65504] before prevRadiance and the sole remaining water_copy/wRad half store. 60000+60000: half Inf -> finite 65504. Representable nonnegative stores unchanged. This deliberately saturates unrepresentable radiance; no wider storage/ABI change.
- F05 confirmed/deferred: mantissa masking misses packed-format subnormals/exponent limits. No small change claimed without verifying actual R11G11B10 boundary/overflow store behavior; no verified GPU boundary fixture is available. Deferred per the user's conditional scope.
- Tests: cons_rr_math (5), cons_shader_bounds (6), cons_reprojection (4), plus updated gi_far_roof caller wiring (2), all pass; three new suites registered in the single CMake foreach list.
- The existing water shader gate now overlays only exact F03/F06 source corrections onto its historical baseline before running the original full-module identity/retirement proofs. Four overlay fixtures pass (exact changes, idempotence, unknown/duplicate rejection, CRLF); the gate passes in 93.74 s. No SPIR-V differences are waived.
- Full-suite testing found and fixed the offline tools/pt_reference.hlsl C10 caller too; invalid lookup retains the hidden-hit classification and fresh PT shading. No other tool HLSL callers remain.
- Requested release_shader_equivalence.py --baseline e0200e2a cannot audit this already-cleaned baseline: removed SpecIds are [], while its contract requires nine removals; its policy receipt also targets the older policy. Tool unchanged per scope. Shared cache is read-only in this sandbox; use a local copy of its 16 MB index and small SPIR-V blobs.
- Fallback proof: tools/shader_identity.py --baseline e0200e2a --json .scratch/cons-shaders-identity.json: 214 unique requests, 64 byte-identical, 150 DIFFERENT, zero compile failures (392.5 s; exit 1 is expected for deliberate differences). Every DIFFERENT request reaches edited shader code; all unrelated requests are byte-identical.
- Exact request strings and their edited include dependencies: tests/fixtures/cons_shaders_identity.json. Raw CLI counts include three duplicate unchanged requests: 67/217 identical. shaders/variants.txt delta: 0; no added/removed requests.
- Final scripts/build.ps1 -Test with the writable cache: PASS, 170/170 CTests (274.03 s). Earlier cache wait and the three initial test failures were corrected before this run. Full log: .scratch/cons-shaders-build-final.log.
- Size limit: all changed source/test/progress files are below 20 MB. The required build generated 10 larger ignored files (six PDB/ILK files in bin/, four copied nvngx DLLs in bin/sl{,_dev}/). Automatic approval review rejected both computed cleanup and explicit-file Remove-Item with "blocked by policy"; those generated files remain. No recursive deletion was attempted.
- No commits or game launch. GPU cost and live appearance unmeasured under the user's no-game instruction; no quality/live pass claimed.

## Product consolidation fixes (branch cons-product, verified offline; C13 deferred, 2026-11-04)

- P1 fixed: removed water hit shading/cost, alpha spill and pak publication from opt-in.
  Release policy resets all four with experimental_options either 0 or 1; Fresnel/hair stay listed.
- C13 deferred in part: selection and flip now share opt-in visibility. Fresnel On is explicitly
  Apply::Restart (settings.h, water pipeline creation); preserving restart exclusions forbids
  making it an F10 target. The review omitted this prerequisite. No startup safety bypass added.
  Tests reject stale Fresnel targets and cover two successive live hair flips with opt-in.
- P2 fixed: null inherited environment fails launch safely; pure environment composition preserves
  Windows drive entries, sorting and double NUL termination. No process is launched by tests.
- P3 fixed: WebView initialization callbacks own shared Host state; closed callbacks stop initialization
  and close late controllers. Exit joins workers and releases controller/view before COM shutdown.
- P4 fixed: ZIP installer stages and hashes payloads, journals previous files, restores payload and
  metadata on errors; keeps recovery files if restoration itself fails. No lifecycle/ISS/package edits.
- P5 fixed: collision backups use unique temporary names, hash verification, then atomic rename;
  failed backup temporaries are cleaned and cannot be adopted on retry.
- P6 fixed: read errors clear partial text and latch settings unreadable; CLI/native/WebView saves
  refuse to overwrite unreadable files. Missing optional INIs remain valid.
- P7 fixed: failed FlushFileBuffers/CloseHandle prevents replacement and reports failure.
- P8 fixed: game-folder persistence returns its error; native Save and WebView Browse display it.
- P9 fixed: failed CreateWindow stops WebView initialization and skips the message loop.
- P10 fixed: native fallback applies opt-in visibility in cards, choices, dependency previews and
  preset review; persists/discards the switch and resets hidden experimental values like WebView.
- P11 fixed: release policy preserves explicit OMM disable from hardware clamping, including dyn/fine LOD.
- P12 fixed: launch plans also remove BG3RTX_ASSETDB, BG3RTX_SE_DIR and BG3RTX_SAVE_LOG, case-insensitively.
- T1 fixed: classifier carries tooling requirement independently of shader scope; merge-green runs
  tooling checks for mixed shader/tooling bundles (and conservatively for full/unknown scope).
- D6 fixed: active inventory 32 -> 17; retired rejected pak/GPU/water-hit and combined scheduling.
  Kept every generated fixture and frozen lint input; five batch READMEs now state outcomes.
- D7 fixed: removed native CLEAN ID/state/handlers; kept CLI --clean and corrected main.cpp comment.
- Tests: settings 4,755 checks, launcher launch/I/O 26 checks, install failure paths 10 tests,
  merge scope 9 tests, batch lint 9 tests, renderer/native ownership contracts pass.
- Required build/CTest passed: 152/152, 0 failures, 171.83 s CTest; incremental build 4 s.
  Used worktree-local BG3RTX_SHADER_CACHE (read-only water cache seed); shared cache was sandbox-blocked.
  gen_settings_defaults.py reports already up to date; git diff --check passes.
- Default shaders unchanged; variants.txt request delta 0; no new GPU pass or live/cost claim.
  No commits, game launches, watched queue writes, save access, or changed source files over 20 MB.

## Layer consolidation (branch cons-layer, offline fixes verified; 3 deferred, 2026-11-04)

- L1 fixed: unfenced image/view/buffer destroys retain ownership and backing memory. Retry budget: 128 entries per tick; growth logged at powers of two. Retained count cannot safely be capped while the GPU remains stalled.
- L2 fixed: a lifetime timeout retains the object for a later fence/idle completion; device-lost fault handling remains active.
- L3 fixed: FG input-consumption timeout retains tagged objects; disabling FG does not bypass an outstanding consumption fence.
- L4 confirmed and fixed: removed both 240-frame completion fallbacks (deferred objects and scene slots).
- L5 confirmed and fixed: lifetime pruning uses completion from the object's own queue.
- L6 confirmed and fixed: applied FG mode/flags/running state and enable counters commit only on SDK success.
- L7 fixed: NGX and RR init retries require complete state; failed partial initialization unwinds.
- L8 fixed: failed image-view creation unwinds image/memory and returns failure.
- L9 fixed: listed image/buffer memory bindings check results and unwind failed allocations.
- L10 fixed: failed reset/begin/end recording returns before submission; completed slots no longer retain an unsignaled used fence.
- L11 confirmed, deferred: RR's 256 descriptor sets have no completion ownership. A correct fix needs recording/submission ownership, not a larger ring or an arbitrary wait.
- L12 confirmed, deferred: NGX guide initialization is recorded before submit. A local pending-owner guard rejects valid separately recorded producer/consumer batches; needs dependency validation at submission.
- L13 confirmed and fixed: a separate guide mutex protects resize and the SSS view accessor without reversing NGX/RR lock order.
- L14 fixed: TOC, record and string ranges check offset <= length and size <= length - offset before addition/pointer arithmetic.
- L15 fixed: alpha dummy/VT initialization publishes on successful submission; discard resets pending ownership. An initial upload held unsubmitted across frames blocks another recording; the one-build-per-frame scene guard preserves normal same-frame batches.
- L16 fixed: device teardown clears dummy/VT upload ownership and per-set initialization latches.
- L17 confirmed, deferred: temporal history can name an unsubmitted producer. As with L12, preserving valid cross-recording batches requires submission-dependency tracking.
- L18 fixed: reset/free/begin/pool discard cancels unsubmitted FG copy leases and restores prior layout/fence; submitted leases remain GPU-owned.
- L19 fixed: thread-local DXC COM ownership releases all interfaces at thread exit and partial-init failure; explicit release is idempotent.
- L20 fixed: write failure or close/flush failure returns false; failed manifest writes remove the temporary file.
- L21 confirmed and fixed: core fault state is atomic; exchange emits only the first fault log.
- R1 fixed: failed diagnostic pipeline creation destroys partially created handles before retry.
- Tests: seven malformed DB files plus one valid control exercise the real loader in fresh processes; layer_cpu_test checks COM cleanup and buffered close failure; resource_retirement_test checks queue ownership, pending memory and 128-entry budget; fg_copy_lease_test adds upload/cancellation cases; layer_init_failure_test runs 18 injected failures with leak-free retries and FG state transitions; recording_lifecycle_test checks integration.
- Validation: build passed; all 6 targeted regressions passed (1.17 s CTest). Full suite: 155/156 passed in 292.11 s; water_surface_shader_test cannot open/write the shared SQLite cache outside the sandbox (first run: readonly database; final: unable to open database file). Logs: build/cons-layer-full-tests.log and build/cons-layer-targeted.log. No shaders, shader requests, presets or ABI changed (variant request delta 0). No game launched; no commit.
- New blocking waits: none. Existing destroy limits remain 2 s for lifetime and 1 s for FG (up to 3 s serially); deferred retries use zero-time polls.
- Live/GPU cost: not measured (offline-only authorization). Coordinator should review deferred findings before qualification.

## Core consolidation (branch cons-core, offline verified, 2026-11-04)

- C1 fixed: sun DSL/pool/sets built locally, cleaned on failure, published together; retries never see a partial ring.
- C2 fixed: emissive layout replacement builds all three layouts locally; failure cleans candidates and preserves old state.
- C3 fixed: removed the unused probe pipeline and per-frame shader request; census callbacks remain.
- C5 fixed: make_layout publishes its DSL/pipeline-layout pair only after success; failed candidates are destroyed.
- C6 fixed: reflection layout replacement preserves old state on failure and retires every cached RF/SHaRC variant once.
- C9 fixed: water pass-end read g_csun/g_cls/g_crr before g_mu, while lighting writes them under g_mu;
  the read now shares that lock (host callbacks may overlap under shared hot-reload locks).
- C11 fixed: both reflection and compose require want_hit before hit layouts/pipeline requests;
  key off also skips material-use registration and world-light allocation/preparation.
- C4 not confirmed: sun ring has 32 sets, minimum reuse age 8 presents, skips when full;
  no demonstrated pending GPU consumer at reuse. Age is not a completion guarantee; no safety claim added.
- C7 not confirmed: light/trust/water rings have 8 slots; no demonstrated wrap with an unfinished consumer.
  Core ABI 26 exposes no completion token; changing retirement needs a coordinated host contract.
- C8 not confirmed: image-init/parity do advance on recording, but no reproduced discarded GI producer;
  core has no submit/discard notification. Do not guess a repair from frame age or change temporal behavior.
- C12 not confirmed: mapped counter reads use an 8-slot ring, not a covering fence;
  no demonstrated unfinished write at consumption. Same host completion-contract limitation as C7.
- C10 deferred to cleanup/DXC identity stream as assigned; shaders and variants untouched (request delta 0).
- D3 fixed in owned files: removed probe-template claims, obsolete water defaults/uncomposed wording,
  and RR one-ray comment (Ultra uses two). settings.h wf9 blocker comment belongs to cons-product.
- D8: fresh full /W3 build compiled all 303 targets/actions, including layer/core/common/launcher.
  Core warnings 0; common C4996 sites 2 -> 0 (_fsopen with original sharing, bounded sscanf_s;
  non-MSVC paths preserved). Live-log sharing is covered by the default-writer test.
  Remaining C4996: layer/alphatest.cpp:425,478 (sscanf); tools/xfb_patch_test.cpp:52 (getenv);
  tests/log_rotate_test.cpp:10,12,27,28 and tests/assetdb_test.cpp:56,197,224 (fopen). Launcher warnings 0.
- Tests added: core_resource_init_test (213 checks against extracted production functions with fake Vulkan,
  allocation failure/retry, layout replacement failure, 9 cached variants retired once), plus 3 source guards.
  Existing log_rotate/vt_alpha_masks tests extended for failed/shared default open and maximum-length filename.
- Validation: all 152 CTests passed in 141.37 s; after the final log-sharing adjustment, rebuild + 3 affected
  CTests passed again. Initial shared-cache access failed outside the sandbox; local cache snapshot retry passed.
- VS bootstrap prints "vswhere.exe not recognized"; the outer shell reported 1 despite the full test pass.
  Focused invocation explicitly returned build.ps1's LASTEXITCODE 0. Bootstrap scripts were not changed.
- Logs: build/cons-core-clean-build.log, build/cons-core-final.log, build/cons-core-focused.log.
  No game launched or GPU cost claimed; rendering shaders/presets and ABI unchanged. No commits made.

## Hair strand path consolidated offline (branch claude-hair-consolidate, READY_FOR_LIVE, 2026-11-04)
Readiness only, not live qualification. The failure-mode table (40 hair rows, all closed, 4 accepted limitations)
is in [RTX_HAIR_PRIMARY section 9](../RTX_HAIR_PRIMARY.md#9-known-failure-modes-and-offline-proofs).
Generated-material rows (hgn1/hgn2) are not hair: TRACK_D_REFLECTIONS owns them.

Orphan counter, exact by construction:
- The latch holds a prepass drop of key K at frame p for G-buffer draws in [p, p + 2]. Epochs and the
  deferred resolved/failed sets are gone.
- An orphan is a latched G-buffer draw drawn natively without fallback, counted at the draw. The new line:
  `[hair primary] depth orphans (last 60 frames, first 4; key prefix:prepass frame_index,draw frame_index,reason,pipeline): none`.
- A key with an uncovered sibling pipeline blocks prepass drops for 300 frames.
- hv9 frame 2631 (3 "orphans" with 180/180/180 swaps and 0 fallbacks) cannot be replayed (aggregates only).
  It is closed by construction: tests for a true orphan (1), the uncovered sibling (0), the hv9 sequence (0),
  both recording orders, interleaving and a hitch frame.
- prepass_drop_allowed (common/hair_primary.h) covers the hv7 off-by-one (applied now - 1 .. now + 2).

Review findings (independent read-only review of main 6a176bb0, each confirmed in the code):
| Item | Fix | Test |
|---|---|---|
| P0 LOD upload ready before its copy ran | used only after the copy's recording is submitted and done; else redone | test_upload_submission |
| P0 compaction trusted an unsubmitted size query | query_rec; a discarded query redoes build + query | test_compaction_submission |
| P1 UV join readback | copy recording tracked; discarded job retried | test_uv_submission |
| P1 G-buffer swap without the prepass drop | needs the key's latch in [p, p + 2]; else cards + probe | test_gbuffer_depth_latch |
| P1 velocity without ownership | accepted marker must carry this tag and the key-map slot | hair_groom_test, marker fixture |
| P2 count slots unsubmitted | slot dropped without evidence | test_count_submission |
| P2 tag low 24 bits 0 | count skips such tags | hair_groom_test |
| P2 "strand pixels" were card counts | card and accepted totals logged separately | test_card_accepted_totals |
| P2 refused never reset, mixed with feedback | separate per-period counters | test_period_failures |
Shared helper: readback_state(frame, done, submitted) -> Wait / Drop / Ready.

Other proofs: hair_prefetch_test (every hair prefetch request is in shaders/variants.txt, +3 plain hair requests,
0 new compiles); hair_patterns_test (the hv9 gate regexes match lines rendered from their source formats).
hv9-visual: orphan gates judged at the window end, after the PNG capture.

Verification (reviewer, 2026-11-04): build + 153 CTests; `live-flow: 1 jobs, 0 failing`; check_patterns
101 patterns, 0 missing, 0 forbidden; shader identity vs 6a176bb0: 261/263 byte-identical, the two
differences are rt/hair_primary_count and rt/hair_primary_velocity (key-on only). ABI unchanged.
Not measured: live image, candidate GPU cost (historical hv9 G-buffer 0.197 ms/frame).
- F01: overflow-safe soup division bound; F08: fixed-depth pixel ray, 1.4 M torn seal mixes / 0 passes; F09: accepted same-groom/frame colour mixes. Section 9 records proofs and pointers.
- Review checks: shader validation 2/2; identity vs main 2f81c3d0: 259/263 identical, only hair groom update, G-buffer, count and velocity differ; request delta 0. Build + 161/161 CTests passed (240.15 s).

## Today's merges (all pushed)

- bundle32 f051a7bd: short windows, mg-scope, gi1/ts1 jobs, cp5 startup, generated-row fix, water compose
  reasons, hair latch.
- bundle33 a48aaba0: scene-wait traces, water-binding fix, roof-leak fix, docs sync, licences and lean layer,
  hair hv9 job, appearance decisions.
- trace-sizing 6a176bb0: trace windows from the capture-stall model; hv9/ts1 job repairs.
- bundle34 ccbbb1d2: RC verify script, outcomes, gi1 analyzer fix, Tileset job fix.
- History rewrite (user-approved): a 213 MB corpus removed from 17 local branches; corpora now in .gitignore.

## Lessons written down

- Check blobs > 20 MB before every commit/merge; generated corpora never go in Git.
- Counters behind a debug dispatch (rt_debug=9) print zeros otherwise: a gate on them needs that dispatch
  and a window that waits for the pipeline build.
- Tileset and other startup-latched keys need dev=1 in jobs, or the release policy resets them.

Full folded branch notes of today: [archive](archive/PROGRESS_CODEX_2026-11-04.md).
Earlier: [night](archive/PROGRESS_20261104_night.md), [11-01](archive/PROGRESS_CODEX_2026-11-01.md).
