## Settings policy merge fix (branch perf-bundle, verified offline, 2026-11-02)
- The schema equality gate was correct: the three perf entries lacked A1 category, cost and maturity metadata in the generated snapshot.
- Marked perf_hook_state developer-only, matching perf_hitmat_lists and perf_submit_batch. All three remain default off and hidden from players, including Off choices.
- Updated the experimental count and developer list expectations; added visibility, maturity and default/preset regression checks for all three perf keys. VRAM keys remain automatic and default on.
- Regenerated data/settings_schema.json. Defaults generation and --check pass; settings_test: 2366 checks, 0 failed.
- Build + CTests: 100% tests passed, 0 tests failed out of 109 (74.75 s); native_hit_gpu_fixture, frame_replay_test and pt_reference_test disabled as requested for shader fixtures.
- No rendering, ABI or shader request changes (variants delta 0). No live test needed for this policy fix.
