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
