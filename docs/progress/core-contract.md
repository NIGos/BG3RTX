## Core completion contract audit (branch core-contract, ABI_BLOCKED, 2026-11-06)

- C4 not confirmed for ordered submitted frames: eight completion-guarded scene slots x two
  handout frames x two sun sets = 32, matching the descriptor ring. Delayed/discarded
  recordings remain outside this proof; no observed BG3 overlap claimed.
- C7/C12 confirmed synchronization gaps: five scene builds at frames 1,3,5,7,9 permit
  eight-slot upload/readback wrap at frame 9 while frame 1 can remain pending.
  Trust staging precedes scene availability; mapped reads do not wait for completion.
- C8 confirmed recording/discard hazard: failed EndCommandBuffer/reset can discard
  initialization/history while g_images_ready/parity remain committed. Normal ordered
  GPU-only ping-pong does not require CPU completion between frames.
- Host life_gpu_done exists privately; current ABI 27 exports only the CPU frame index.
  FG allows up to three generated frames per app frame, not three pending app frames.
  Present fences poll without throttling; FG copy-slot tokens protect only those slots.
- Stopped before ABI changes as requested: completion must cover the consuming recording;
  C8 also needs submit/discard notification. No runtime, settings, or shader edits.
- Updated all four CONSOLIDATION rows and added source-linked proof/counterexamples there.
- Validation: scripts/build.ps1 -Test PASS 195/195 (build 61 s; CTest 248 s), BelowNormal;
  git diff --check PASS. No game launch, queue writes, or repository git writes.
- Shader request delta 0; all shader inputs unchanged, so ordinary/key-off bytes unchanged.
  No new CPU logic or build-dependent tests; merge_scope.py needs no registration change.
