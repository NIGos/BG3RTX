## Pak UV pending fix (branch pakjoin-pending, READY_FOR_LIVE, 2026-11-03)
- cp4 raw UV format 83 is half2: raw float2 binding stayed 0xff, blocking XFB enqueue and retrieval (0 attempts, 422,707 pending/draws).
- Keep raw CPU witness gates; decoded XFB keys and invalidation cover all per-vertex bindings. Publication gates and default-off key stay intact; ABI/shader request delta 0.
- Production readback fixture replays discovery -> enqueue -> copy -> submit retirement -> later-recording attempt, including packed UV, changed binding and unique/missing/ambiguous outcomes.
- cp4 ON 9.1 -> 19.1 s: rounded observed 9 s line bound plus settings/straddle; require attempts > 0. Predict unique minority, missing majority, possible ambiguity; full witnesses may stay 0.
- Host build, 7 focused CTests and cp4 offline preflight pass; no game launch, commit or GPU cost measurement. Earlier fragment is folded in docs/archive/PROGRESS_20261103_evening.md.
