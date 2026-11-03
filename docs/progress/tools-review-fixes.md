## Runner and offline tool repairs (branch tools-review-fixes, kept, 2026-11-03)
- Fixed slice phase/label rebasing, explicit queue completion, and owned merge recovery with abort/reset verification.
- Fixed single-value warmup and startup checks, shared midnight origin, shared plan fields, and prerequisite preservation.
- Regression checks: 25 Python tests and 3 new synthetic PowerShell tests pass; combined dryrun passes all 3 manifests.
- Full lint: 104 jobs, 75 failures, same failing count as baseline; every previously passing audited job still passes.
- Re-extracted 2 Dialogue3 runs offline; original forbidden-log failures remain. Camp extraction rejects manifest drift.
- Existing queue tests: 6 pass, 2 blocked by missing py/pwsh; existing camera test fails its water turn-window assumption.
- No game, real queue, commits or repository Git metadata changes. Shader request delta 0; offline receipts in captures/offline/tools-review-extracted.
