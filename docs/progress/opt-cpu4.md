## CPU pre-draw optimization 4 (branch opt-cpu4, rejected, 2026-11-05)

- cpu_opt3 qualified at the inn on Ultra: capture total -0.24 ms/frame vs
  OFF spread 0.054 ms; default 1. CPU1/CPU2 remain 1.
- cpu_opt4 inn Ultra A/B: capture hook bodies +0.47 / +0.54 ms/frame vs OFF
  spread 0.104 ms. Rejected; evidence: captures/queue/logs/opt-cpu4-inn.*.log
  and its .cpu.md report. opt-final removes code, key, tests and opt_r5 job.
- Historical candidate used descriptor receipts, mapped-address/buffer-facts
  caches and persistent submit matrix slots; ABI and shader requests unchanged.
- Offline fixtures passed descriptor lifecycle and mapped/matrix sequences.
  Synthetic matrix lookup 0.1313 -> 0.1017 ms/frame did not predict game savings.
- Historical build/test passed 186/186 (183.44 s CTest); frozen lint receipts
  remain. The live rejection supersedes the former READY_FOR_LIVE status.
