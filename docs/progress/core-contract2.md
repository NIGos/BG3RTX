## Core completion contract (branch core-contract2, READY_FOR_LIVE, 2026-11-06)

- ABI 27 -> 28: present-queue completed frame and retained exact recording lifecycle queries.
  Both VkLayer_bg3rtx.dll and bg3rtx_core.dll rebuilt together.
- Reused layer-submit2 submit/discard and secondary snapshots. Per-queue fences cover submit
  frame + 1; a covering device-idle wait also completes work. Unknown queries fail closed.
- C7: light/trust/water mapped slots retain all consumer recordings. Reuse requires discard
  plus GPU completion; busy selected slot skips the update and logs once.
- C8: GI initialization/parity commit after submission or roll back after discard;
  stale snapshots are rejected. Ordered submitted GPU history does not wait for completion.
- C12: GI/occlusion counters read only submitted, discarded, completed slots; busy slots preserved.
- C4 remains not confirmed, with the existing ordered-frame proof and limitations unchanged.
- CPU gates model 2/3 frames in flight, FG display ticks, discarded recordings, a stall,
  resubmission, multiple consumers, queue-specific completion and balanced recording references.
- Shader identity PASS 218/218 by identical inputs; variants.txt request delta 0.
- Validation: BelowNormal scripts/build.ps1 -Test PASS 199/199; build 85 s, CTest 309 s.
  git diff --check PASS. Steady models: 80 frames each at 2/3 in flight, zero skips, 72 reads each.
- No game, queue preparation or Git writes. Coordinator Ultra smoke pending;
  check no completion-guard warnings in steady state and existing Ultra effect/temporal gates.
- GPU cost unmeasured (offline only); no new GPU commands or shader changes.
