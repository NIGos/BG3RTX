## Unexpected game exits (branch runner-exit-crash, offline verified, 2026-11-04)
- Classify unrequested exits as CRASH without a dump/device-lost line; retain code, phase -1, last 3 log lines and available dump paths. Catch exits before cleanup; normal/fail-fast runner closes stay ordinary outcomes.
- Synthetic completion/crash tests pass, including load exit -36863 (0xFFFF7001), device loss and cleanup race. No game, real queue or Git metadata writes.
