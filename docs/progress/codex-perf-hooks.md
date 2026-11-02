## CPU hook state and submit matrices (branch codex-perf-hooks, READY_FOR_LIVE, 2026-11-02)
- Implements CPU audit ranks 2 and 3 behind `perf_hook_state=0`; Experimental GUI control, off in all presets.
- Draw bookkeeping and capture/material binding queries accept the current recording state. Descriptors,
  offsets, mapped values and lifetime notes remain current. The exported host API and ABI are unchanged.
- Immutable pipeline snapshots and eligibility use one owned entry per thread, invalidated by the
  pipeline registry generation on create/destroy, including handle reuse. No CmdState pointer is cached.
- Each submit gathers each exact triple of 16-byte matrix row ranges once, validates it once and scatters
  to all destinations. Nothing survives the submit. Invalid job NaN, instance mask and late/bad counts remain.
- Existing `[cpu] capture hooks` adds draw bookkeeping ms/frame and a key tag; resets partial toggle windows.
  Existing matrix line adds unique matrices, builds, submit samples and key tag; time is divided by builds,
  with one build per submitted nonempty fixup list. Both require `cpu_timing=1`; hook timers need diagnostics.
- Validation: explicit C++ target build passed; 100/100 permitted CTests passed in 17.89 s.
  Cache/gather/scatter tests cover generation changes, same-handle replacement, duplicates, different row
  triples, fresh next-submit reads, NaN/infinity/singular rejection and valid/invalid destination semantics.
- Excluded: native_hit_gpu_fixture, frame_replay_test, pt_reference_test, water_audit_test, shader_identity_test.
  The first build selector accidentally included Ninja's `all` alias and ran the fixture DXC command once;
  corrected to explicit C++ targets. No further shader tools were run. Shader request delta 0.
- No game launch, queue write, git metadata write or commit. No measured CPU saving or GPU cost claim.

### A/B plan (prepared only; not authorized or queued)
- Prediction artifact: [predicted.json](codex-perf-hooks/predicted.json). Keep both changes under the one key.
- After explicit steering, use one session per save: S3 camp for hooks, S8 Water Queens House for matrices.
  Keep qualified Ultra keys on; restart once for the rebuilt host. Use `visible_unfocused_fg` at the PC.
- Alternate OFF/ON/OFF/ON in one moving session, identical static/pan/rotation phases, 900 steady frames
  per window. Require at least two complete 300-frame hook samples and 300 matrix submit samples in each
  judged window; extend it if sparse builds cannot supply those samples. No concurrent builds or compiles.
- Future job `patterns` and `expect` must include capture hooks, matrix copies, hit material, matrix validity,
  scene wait, scene-build GPU, rt passes, geo drops, gpu GI/LS/RF/water and watchdog draws/presents.
  Its `expect_every` must require mode-tagged hook AND matrix lines in EVERY ON window; no missing samples.
- Decide hook work from pre-draw + draw bookkeeping (exclude the driver draw slot); normalize work by draws.
  Compare matrix ms/build and unique/matrices. Predicted conditional gains: 0.1-0.3 ms/frame hooks and
  0.1-0.3 ms/build matrices. Zero gain or a regression is possible; these are estimates, not gates.
- Reject added invalid matrices, late/stale coverage, scene-wait timeouts, previous-frame RT passes or capacity
  drops. Compare workload-normalized late counters; do not demand zero when the game performs late writes.
- Derive timing/coverage tolerances from OFF repeats. Claim improvement only when its interval excludes zero.
  Record GPU costs from existing lines; follow with the same-key Ultra smoke. Keep short expensive primary
  reference/coverage and static float-mean captures separate from clean timing windows; never use PNG energy.
- Energy, current-frame primary consistency and GI/LS/RF/water/build costs must remain within OFF repeat noise.
  No observed live result yet. Keep the key off pending these gates.
