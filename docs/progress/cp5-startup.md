## S6 catalogue startup (branch cp5-startup, READY_FOR_LIVE, 2026-11-04)

- Set rt_hit_material_paks=1 at startup so device creation loads the catalogue.
- Keep same-run OFF/ON toggles: pool_rollover refreshes paks_key from that same
  setting each frame; paks_rows gates publication with the startup capability.
- Regenerated cp5; 15 phases and 203.3 s after load remain unchanged. Current
  corpus refreshes trace timing metadata. No shader requests or ABI changes.
- Offline: live_flow preflight passes (1 job); batch_jobs_lint passes (6 tests).
- Fake queue dry-run passes both missing-evidence rejection and structural runs.
- Live check: require catalogue load of 40,983 candidates and positive witnessed
  publication/verified basis in both ON diagnostics at Adamantine Forge (S6).
- No game launched. Publication, image/energy and GPU cost remain unqualified.
