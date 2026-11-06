## FG release audit, Round 3 (branch fgx3, offline verified, 2026-11-06)

- Baseline da6ffdf8; full checklist and exact references:
  [analysis](../evidence/fgx3-analysis.md), Round 3 audit and linked live/release tables.
- Warmed S8 x3 gives SDK/proxy ratio 3 at ~30 rendered fps, p99 comparable to x2.
  This does not qualify uncapped foreground throughput. Metering stays rejected/off;
  exact retirement is unmeasured because every new window has 0 FG destroy waits.
- Lead 1 is real at first x2 enable in 4/4 initial loads: proxy mean 34-46 ms,
  up to 108 ms with metering, then ~0.2-0.4 ms. GPU intervals also inflate.
  GetState <=0.002 ms max, ReflexSleep ~0.02 ms mean and zero FG waits exclude
  those operations there. SDK-vs-raw-driver/WSI backpressure remains OPEN.
- Fixed release leaks: raw capture/trace/dev SDK/reference/stat overrides now
  respect packaged zero pins after layering and startup Detailed logging latch.
  Explicit support statistics work with Detailed On; correctness inputs remain.
- Quiet release skips FG/tracker timers/counters/cadence formatting, per-frame
  pace sampling/sorting and premature VRAM driver queries. Required fences stay.
- Routine HUD/UI identity logs are stats-only and de-duplicated: 764 menu UI
  lines represent 8 handle changes. Fault/state/input-decision lines remain.
- Developer fallback checks no longer format/lock per release lookup. Hot cache
  refreshes on startup/files/hotkey changes: 100 unchanged CPU-test polls ->0
  refreshes of the 47-key cache. No measured ms saving is claimed.
- Fixed phase-reset contamination; SDK/CPU/raw/wait counts and ratio baselines
  reset together. Detailed-only rawPresent host/worker timers target the open stall.
- Missing mandatory SDK exports fail closed; generated count respects adapter max.
  Current 5090 x3 still requests 2 generated, max 5. No valid-path default change.
- fps_cap 58 remains a rendered 17,241 us interval, not divided for MFG.
  New lives cap 0; their 30 fps ceiling is not either explicit layer limiter.
- Copy remains default 1 for correctness: median GPU 0.026 ms. NR 6.2-7.1 ms
  is external ownership; no ngx.cpp/dlss5.cpp edit. Camera-cut reset/synthetic
  simulation markers remain integration gaps; exact SDK startup wait is unproved.
- Release values: captures/trace/verboseSDK/stats/timing 0 with dev=0/detail=0;
  fg_input_copy/current_destroy_only/letterbox 1; UI PQ/exact/metering 0 by default.
- Validation: build.ps1 -Test PASS 206/206 (CTest 139.14 s), 4,480 settings checks.
  Lean -ReleaseLayer PASS 15 s, BG3RTX_DIAGNOSTICS=OFF; defaults/diff checks pass.
  Shader request delta 0; ABI unchanged; no new key, game, queue write or push.
