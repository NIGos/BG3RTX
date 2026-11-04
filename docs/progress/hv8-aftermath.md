## Probe Aftermath (hv8-aftermath, READY_FOR_LIVE, 2026-11-04)
- Global device loss with startup aftermath=1 now polls for dump completion for up to 60 s before normal close; logs completion/path or timeout; remains a failure. Hv8 patterns retain the dump path.
- Offline: live_guard 21/21, runner synthetic completion/delayed-write/timeout/opt-out checks PASS; hv8 lint 0 failures (81 VALID logs; historical replay/readiness still unproven). No game launched; no commit.
