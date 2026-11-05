## Queue cancellation, ReShade isolation and release-policy lint (branch queue-tooling, offline verified with limits, 2026-11-05)

- T2 confirmed: single-run cancellation/finally killed only wrapper PIDs. Both owned phase and command roots now use taskkill /T /F after the existing cooperative runner grace period.
- ReShade disable was missing from auto-run. Set DISABLE_VK_LAYER_reshade_1=1 immediately before launch and log VK_LAYER_reshade as disabled. Player launcher unchanged. Startup crashes were reported by the user, not reproduced offline.
- Release-policy lint gap confirmed: use settings_schema.json developer metadata and zero-choice keys to reject incompatible explicit job/plan overrides with dev=0 unless experimental_options=1. Covers cumulative changes, compound keys and inverted choices; harmless resets remain allowed.
- Tests: live_flow_test.py 76 passed; auto_run_trace_test.ps1 17 checks passed; queue_cancellation_test.ps1 -TreeOnly -MockTreeKill 8 checks passed. Updated the existing trace test to follow the current phaseFinished/export completion condition.
- Native scratch-tree integration attempted: taskkill denied by the sandbox (Access denied). Exact scratch processes were stopped individually. Native descendant termination remains to verify outside this sandbox; mocked /PID /T /F wiring passed.
- Active audit: live_flow.py --frozen --active, 17 jobs, 0 failing. Folders: edge_flicker, hair_crowd, hair_validation, post_temporal, smoke_ultra, water_flicker_wf9, water_surface_fresnel. No newly failing jobs. Historical replay coverage is unproven for all 17; lint is structural, not a live qualification.
- Optional feature/preset gate lint deferred: requires preset expansion plus release-policy resolution to avoid false positives.
- Full scripts/build.ps1 -Test deferred: BG3 was running during verification; repository rules forbid builds while playing. No game launch, real queue writes or Git writes. Shader request delta: 0.
