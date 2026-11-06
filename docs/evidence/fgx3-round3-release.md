# Round 3 release/present policy audit

Code pointers below refer to this Round 3 worktree after the fixes. The main
live/session findings and measured costs are in fgx3-round3-lives.md.

| Item | Verdict | Evidence and action |
|---|---|---|
| Release diagnostic layering | BUG fixed | `common/settings.h:1246-1266`: the previous policy reset hidden GUI choices but left raw FG capture/trace/stat overrides intact. All zero diagnostic pins from `kReleaseDefaults` now apply after user-file layering. `geo_capture` is explicitly excluded because it can be a correctness input. |
| Startup-latched Detailed logging | BUG fixed | `layer/util.cpp:362-363`: reapply the policy after restoring the startup value. Saving support logging On for the next launch must not let stale rt_stats/period keys enable statistics in the currently quiet process. |
| Pacing statistics in quiet release | BUG fixed | `layer/layer.cpp:808-832`: collecting QPC samples, pushing a vector, copying and sorting 600 values previously ran even when `log_stats` discarded the line. The gate now precedes that work; disabling statistics clears old samples, preventing a later window from inheriting a quiet-mode gap. CPU cost was not independently measured. |
| VRAM health summary | BUG fixed | `layer/layer.cpp:640-650`: the one-minute quiet throttle now precedes the driver memory-budget query. Quiet present samples its clock once per 64 frames and queries/logs only when the summary is due. Health monitoring remains active. |
| Fallback-literal developer checks | BUG fixed | `layer/util.cpp:140,375,455-469`: every mismatched fallback previously formatted a string and entered a second mutex/map lookup even after the warning had already been emitted. A cached dev-mode atomic now gates that validation before formatting. Release uses zero such diagnostic checks; normal settings lookup semantics stay unchanged. |
| Base logging sink | COST | `layer/util.cpp:101-123` formats the message/time, holds `g_log_mu`, writes, and calls OutputDebugStringA. `common/log_rotate.h:80` flushes every emitted line. This preserves crash/runner observability; no ms duration is available. Fix hot caller spam rather than introducing a logger framework. |
| Config hot-cache refresh | BUG fixed / remaining COST | `layer/util.cpp:320-328` now refreshes 47 cached settings only at startup or after an actual file/hotkey change. The existing poll mutex covers mutation and publication together, preventing concurrent presents from leaving a stale mixed cache. An extracted actual-cfg_poll CPU fixture proves one startup refresh, zero refreshes over 100 unchanged polls, same-poll file/master/experimental/NR refresh, and one refresh for simultaneous changes. The three file-mtime checks and hotkey-edge polling remain per present; their ms cost is unmeasured. |
| Queue serialization | OK / COST | `layer/layer.cpp:514,580,873` protects actual game submit/present calls with the common queue lock. It can wait on another call, but it is not a hidden frame limiter. Streamline-side blocking attribution remains in the FG audit. |
| Background hooks | OK | `layer/background.cpp:121` returns before patching unless BG3RTX_BACKGROUND=1. Its Sleep(2000) is on a module-watcher thread (:138), not a present/submit thread. Only the first 20 dropped SDL focus events log (:52). Normal release has no background thread or artificial frame cap from this file. The game's own unfocused cap is separate. |

## Release values and exceptions

`data/release.ini` and generated `kReleaseDefaults` now explicitly pin:

| Controls | Release value / behavior |
|---|---|
| fg_snap, fg_snap_any, fg_snap_full | 0, enforced for dev=0 even with Detailed logging On |
| fg_log_presents, fg_log_camera, sl_log_barriers, log_presents, log_procs | 0, enforced for dev=0 |
| sl_dev, sl_verbose, fg_only_interpolated | 0, enforced for dev=0 |
| pan_capture, pan_prepare, pan_inputs, pan_edge, replay_pack | 0, enforced for dev=0 |
| rt/ls/rf/sharc_stats, cpu_timing, geo_timing, rt_stats_period_frames, rt_stats_reset | 0 in ordinary dev=0 + detailed_logging=0. Explicit support statistics survive when startup Detailed logging is On. |
| rt_primary_check/epoch/identity/fade_class, rr_post_check and the other packaged debug/reference/dump pins | 0, enforced for dev=0 regardless of Detailed logging |
| fg_input_copy | Default/shipped 1, preserved as a correctness copy; not switched off as a diagnostic. An explicit selection remains unchanged by this patch. |
| fg_current_destroy_only / fg_destroy_wait_exact / fg_present_metering | Defaults 1 / 0 / 0; no change to the two existing experimental opt-in policies |
| fg, fps_cap, reflex, geo_capture | Explicit selections preserved. In particular fg=3, fps_cap=58, reflex=2 and geo_capture=1 survive the policy tests. |

Detailed logging remains a support control, latched at startup. It enables the
central statistics gate, not developer frame captures or a development SDK.
Dev=1 keeps raw diagnostics available for intentional offline/live QA. No new
settings key, default effect change, ABI change or shader variant was added.

Tests added to `settings_test` exercise stale raw captures/stats, the expensive
primary-reference override, support logging, developer escape, correctness
capture preservation and the startup-latch scenario. `gpu_logging_gate_test.py`
adds quiet-present work/driver-query ordering and developer-literal validation
checks. The event-refresh fixture compiles the actual cfg_poll body under MSVC;
all ten Python/CPU guard tests pass (no skips). The coordinator runs the final
complete build and schema generation.
