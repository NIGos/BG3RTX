## FPS windows (branch tools-fps-windows, kept offline, 2026-11-03)
- Per-save diagnostic-state FPS uses 102 readable VALID logs, conservative p10 and a documented cost fallback; lint and fail-fast share the model.
- su1 diag-omm: 9.1 -> 16.4 s. cp4 ON observations: 19.1 -> 32.8 s each. Gates unchanged.
- Regenerated 15 active jobs plus combined manifests; separate crowd plans prevent save-dependent overwrites. Full window table: tools/live_batches/fps_windows_report.md.
- Replay checked 108 jobs; 106 lack some historical gate evidence. Exact failures: captures/offline/fps_windows_replay.json. No rendering pass claimed.
- lean_dryrun delegates duration rules; 5 hair jobs and both 5-job fake-queue paths pass. Aggregate estimate 71.1 min exceeds the retained 65 min limit.
- cp4 and combined camp/S8 remain blocked by readiness/launch budgets; combined receipts now retain these failures. No game, queue submission or Git metadata writes.
- Validation: 147/147 CTests pass; 58 focused tooling tests pass. Shader request delta 0.
