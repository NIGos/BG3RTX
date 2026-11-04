## Unexpected game exits (branch runner-exit-crash, offline verified, 2026-11-04)
- Classify unrequested exits as CRASH without a dump/device-lost line; retain code, phase -1, last 3 log lines and available dump paths. Catch exits before cleanup; normal/fail-fast runner closes stay ordinary outcomes.
- Synthetic completion/crash tests pass, including load exit -36863 (0xFFFF7001), device loss and cleanup race. No game, real queue or Git metadata writes.

## Readiness and runtime guards (branch runner-exit-crash, offline verified, 2026-11-04)
- Defer bursts until readiness; preserve measured cutoff. Fail-fast uses first_only and shared line matching. Require fresh Fresnel state and RR resident completions; reconcile RR starts/failures in runner and budget.
- Runtime deadline clamps timeout to max_minutes (default 900 s); median readiness remains an estimate. Python gates and 646 camera checks pass. Active lint: 18/26 pass, identical 8 failures against HEAD.
- No game or commit. Main merge blocked by read-only ORIG_HEAD (6 commits behind); full fake/cancellation tests need missing py, cross-version permission test needs pwsh.
