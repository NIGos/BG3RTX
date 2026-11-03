## Hit material CPU lists and uploads (branch codex-perf-hitmat, READY_FOR_LIVE, 2026-11-02)
- Key: `perf_hitmat_lists=0` by default, live and developer-only; every preset keeps it off.
- Implements CPU audit ranks 1 and 5; shader requests and host ABI unchanged.
- Recording-owned candidates seal at scene preparation; query pairing uses immutable metadata outside the provider lock.
- Row uploads merge adjacent destinations up to 64 KiB; independent join jobs group by chunk.
- Exact GPU clip/UV checks, instance remap, resource checks and submit-order publication remain required.
- CPU timing: `[cpu] hit material` reports window key, mutex waits and query record ms/frame (`cpu_timing=1`).
- Validation: host/core and CPU test targets build; 98/99 CPU CTests pass (new list/batch tests pass).
- Existing `fold_progress_test` fails: inherited `hm-recipe-rows.md` has 82 lines (limit 40); unchanged here.
- GPU fixture/replay CTests excluded (shader execution). Live CPU/GPU savings unmeasured; no game or queue writes.

### Proposed camp A/B job (not queued; requires separate live authorization)
- Name `cpu-camp-hitmat`; one launch, save `Campsite - 27h`, diagnostics layer, Ultra, FG 2.
- Use `window_mode: visible_unfocused_fg`, session jobs, `hold: 90` minimum, ~2-5 min and <2 GB.
- Keep qualified Ultra keys on, including water, bounce, emission and far-field; existing automatic fixes stay on.
- Set `cpu_timing=1,rt_scene_wait_log_frames=120,rt_timing_frames=120,perf_hitmat_lists=0`.
- Warm shaders before judging; no build, compile or expensive diagnostics in cost windows.
- OFF-A / ON-A / OFF-B / ON-B: same static pose plus repeatable Q/E return pan in each state.
- After each toggle discard the first CPU timing line (300-frame cadence may straddle it).
- Each measured state needs >=600 steady frames (two complete 300-frame CPU windows and pacing sample).
- Derive phase seconds from observed rendered rate; extend holds if complete window lines are missing.
- Separate static OFF/ON/OFF float scene captures with exposure held; compare `dumps.json`, not PNG/bloom.
- Separate short coverage/primary reference windows with `rt_stats=1`; return to 0 before cost windows.
- `patterns`: `[cpu] hit material`, `[hit material capacity]`, `[hit material query]`, `[hit material instanced]`,
  `[rt] scene wait`, `[rt] scene build GPU`, `rt passes`, `gi: trace pass`, LS/RF/water GPU lines, `[pace]`.
- `expect`: scene ready, successful RR/FG, both key values in CPU lines, every named plan window complete.
- `expect_every` for every ON window: CPU key 1, zero scene-wait timeouts, current-frame TLAS with no missing passes;
  in coverage windows require zero source/query/table/stream overflow and complete material/instance counters.
- Compare qualified coverage, mapped rows and remap outcomes at matching poses; no omitted or stale geometry.
- Reject crashes, device loss, scene/capture/mesh/basis capacity drops or lower energy outside OFF-repeat noise.
- Preserve GPU scene-build and GI/LS/RF/water costs within OFF-repeat uncertainty; forecast GPU delta 0 ms.
- CPU forecast in [predicted.json](codex-perf-hitmat/predicted.json): mutex wait -0.2 to -0.5 ms/frame,
  query total -0.2 to -0.4 ms/frame; overlapping envelopes, never sum them or promise frame-time savings.
- Decide from repeated OFF noise/confidence intervals; a delta including zero supports no savings claim.
- Restore key 0 and exposure hold 0 in the session finally/restore block. Confirm later with `smoke_ultra`.


