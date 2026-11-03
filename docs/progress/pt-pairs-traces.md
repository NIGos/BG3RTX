## Trace window sizing (branch pt-pairs-traces, READY_FOR_LIVE, 2026-11-04)
- 004032 missed on-02 dispatch: 2.1 s window lasted 3.803 s; five written latencies were 0.081-0.097 s. VALID p95 0.317 s / 439 requests; written precedes dump completion.
- Budget 4 s dispatch plus serialized export/write across phases; energy 2.1 -> 6.1 s. Keep all gates/traces; 10 balanced pairs >= derived minimum 8, total 864.9 s including readiness/loading (12 would be 927.7 s).
- Offline preflight, 45 live-flow and 9 paired tests pass. No game, queue, commit or git metadata writes; live quality remains unproven.
