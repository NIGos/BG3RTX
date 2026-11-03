## Automatic VRAM management (branch vram-default-on, QUALIFIED vg3, 2026-11-02)

- Kept: `rt_vram_guard=1`, `rt_vram_compact=1`, `rt_capture_elastic=1` by default.
  Removed their Experimental knobs; all presets inherit the defaults without overriding
  deliberate INI A/B settings. `rt_vram_guard_budget_mb=0` remains test-only (real driver budget).
- Evidence: `captures/queue/logs/vg3-s8-low-long-view-fg2.1.221959.log`, sibling runner
  and `.vg3.json` (read-only). S8 port, real budget, matched right/left OFF/ON/OFF turns.
  Post gate PASS, no failures. Four complete capture summaries per turn.
- Opaque dropped draws/frame, peak OFF-a / ON / OFF-b: right 1.78 / 0.39 / 1.20,
  left 1.10 / 0.64 / 1.77. ON excess -1.39 / -1.13 vs tolerances 0.60 / 0.69.
  Means: right 0.465 / 0.1075 / 0.3275 (tolerance 0.1575),
  left 0.2825 / 0.1700 / 0.4575 (tolerance 0.1950). Alpha fallback 0 throughout.
- Float scene energy (exposure-normalised): 862.7885 / 863.6893 / 861.4242,
  ON +0.1836%, band +/-0.92%; OFF-OFF -0.1581%.
- Every ON snapshot within budget: right 19,462-21,092 MB, left 19,475-21,860 MB,
  recovery 16,301-16,402 MB, driver budget 31,419 MB. Scene-wait timeouts 0
  in every gated window; no ON Streamline >100 ms or fence timeout.
- Cost from complete 300-frame GPU intervals within each turn, OFF-a / ON / OFF-b:
  RT passes right 5.363 / 5.464 / 5.657 ms, left 5.321 / 5.269 / 5.287 ms;
  GI right 9.021 / 9.317 / 10.529 ms, left 8.871 / 8.907 / 8.924 ms.
  No dedicated compaction timer; these moving-view totals do not isolate incremental cost.
- smoke_ultra now requires `[mem] ours` within budget in every smoke window (events,
  1 s settle), with the three automatic defaults; existing opaque-drop tolerance stays 0.00.
- Check in game: S8 Water Queens House port, long low-camera view and right/left turns.
  Inspect `[mem] ours`: `(within budget); guard normal` or `LOW`; compare reported
  used MB with budget and watch for stutter/flicker and extra opaque capture drops.
- No ABI/shader changes; `shaders/variants.txt` request delta 0. No game launched.
- Validation: 95 C++ targets built, 99/99 permitted CTests passed (20.21 s);
  settings 979/979 checks, regenerated GUI schema, defaults generator --check clean.
  Six shader-tool/compile-related CTests excluded by task constraint. Smoke capacity,
  scene-wait and exposure lints plus synthetic budget fail-closed checks passed.
- Updated smoke job remains READY_FOR_LIVE, not run. Optional full fake-queue dryrun
  failed here: missing `py` launcher and empty run A; direct Python lints passed.
