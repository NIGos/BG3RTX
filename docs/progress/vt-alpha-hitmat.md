## Native VT hit-material source (branch vt-alpha-hitmat, READY_FOR_LIVE, 2026-11-03)
- Fixed Ultra VT-mask substitution hiding native kind-1 records from hit-material prepass qualification.
- Preserve native records and slots before mask substitution; ray traversal retains the substituted masks.
- Generated and instanced logs now count captures skipped for a missing alpha source (
o source).
- Regression covers masks ON/OFF, native prepass and generated source eligibility, restored indices and reset.
- qs4 regression evidence: camp generated captured/mapped 0/0 (older 134/43); 2S prepass 71 -> 5, inn 61 -> 44.
- Prepared masks-off camp/inn diagnostics; numeric recovery and GPU cost remain unmeasured (no game launch).
- S8 hooks/submit windows 35 s; guard shots wait for ready specs; OFF shots re-centre; Q/E traces during motion.
- Dryrun PASS: 3 invalid without evidence / 3 structurally valid; scene-wait/capacity lints; camera checks 622/622.
- Batch: 884 s phases, <=240 s pipeline waits, 18 traces, 1,110 MB; CPU/GI remain screening only.
- perf_hook_state REJECTED: submit 20.2 -> 26.8 ms, matrix copies 0.31 -> 0.52 ms/build; retain off for dependencies.
- perf_hitmat_lists no gain (1.16 ms both); perf_submit_batch within noise (20.2/20.7 vs 20.3/20.3 ms); both off.
- Validation: build + all 134 CTests passed; QA detectors passed; schema refreshed; diff check passed. Build wrapper exit 1: unavailable vswhere; cached build exit 0.
- No new key, ABI or shader changes; variants delta 0. No commit; git metadata read-only.
