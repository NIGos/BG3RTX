## Final consolidation, stream C: launcher, release docs, verify_rc, tooling (branch claude/final-tools, offline, 2026-11-20)

- Launcher defects fixed (tests in `tests/launcher_ui_test.js` / `.py`):
  - The GPU list (Help & support, 2+ adapters) was rebuilt and closed by every refresh while shaders or hair styles
    prepare (every 2 s). It is now held like a dragged slider until a choice or blur.
  - A WebView page that did not load within 60 s closed the launcher with exit 2 and no window. The player now gets
    the native window (`failed` -> -1); review shots still fail with 2.
  - A preset change listed every moved setting in the strip (6 lines at 640 px). It names three, counts the rest and
    shows all in the tooltip.
  - Preparation's lead said "Runs in the background"; the converter is in the launcher's job object and stops when
    the window closes. It now says it runs while the window is open and continues where it stopped.
- Reviewed, no defect: rapid clicks (absolute values, serial bridge), Undo after preset/opt-in, Retry, restart
  indicator after a game restart (session token), missing game folder, WebView missing -> native. While the game
  runs: one 2 s timer, two process snapshots, one `*.hcap` listing and two small status reads per tick; no WebView
  work when unchanged; minimised = WebView suspended.
- Headless Edge: 21 views x 1280x800, 1000x700, 640x700, 0 layout problems; shots in `docs/ui_rework/shots_final/`.
- Docs: release notes and Nexus changelog no longer name an RTX Hair switch on Home; Nexus description and sticky
  post: no Save, "Choose folder…", keys on Hotkeys (F7 added), four experimental options, no "Disable other Vulkan
  overlays" checkbox (removed long ago), "Create a report…". High preset text names Low-latency scene (settings.h,
  schema). "Softer at distance" cited from the pixel-LOD study (RTX_HAIR_PRIMARY 5v) in RELEASE_EVIDENCE and RC_CHECK.
  HANDOFF: RTX Hair is an opt-in Beta control, not hidden. SCENE_HANDOUT_DESIGN: duplicate 8.6, stale "off by default".
- verify_rc: an existing BG3RTX Apps entry makes the Setup row SKIPPED with the install named (Result "PASS with 1
  skipped row", exit 2), not FAIL. Shared helper `scripts/release/setup_presence.ps1`; test in `script_guards_test`.
- Tooling: Python CTests whose unittest cases skip now report Skipped (`SKIP_REGULAR_EXPRESSION`), not a pass.
  Dead staged batch `hair_pixel_lod` removed. perf_ab and handout_qual already share `sane_gates.py`; gi_1ray_qual
  keeps its historical m4 energy band (decision done).
- Slowest CTests (s): hair_groom 46.5, release_lifecycle 37.3, ps_suite 32.2, launcher_diagnostics_fixtures 32.2,
  merge_green 23.3, release_install 23.1, pt_reference 20.5, qa_detectors 19.1, hair_primary_gpu 17.4,
  frame_replay_ls 11.7 (380 s summed over 237).
- Not committed: `tools/live_batches_staged/dlss5/` fails lint (frozen: readiness query absent from the snapshot;
  live: PNG windows guarantee 0 complete traces).
