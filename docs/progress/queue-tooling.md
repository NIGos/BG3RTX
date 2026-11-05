## Queue cancellation, ReShade isolation and release-policy lint (branch queue-tooling, offline PASS with limits, 2026-11-06)

- T2: owned phase and command roots use taskkill /T /F after cooperative runner cleanup. Mocked tree cancellation: 8 checks pass. Native descendant termination remains unverified here; the previous sandbox attempt denied taskkill.
- Added keep_reshade (boolean, default false). True clears DISABLE_VK_LAYER_reshade_1 before launch; ordinary jobs set it to 1. Both policies are logged. Session reuse includes this field, so differing policies cannot share a game.
- Reproduced merge-green failures: two obsolete compact-hit assertions and release-profile lint rejecting qualified preset values. Tests now use water Fresnel; lint accepts current schema preset choices while rejecting unqualified overrides.
- data/settings_schema.json matches current main, including DLSS 5 and the removed compact-hit knob. No schema copy or hardcoded knob list added.
- merge_scope.py --ref HEAD --run tooling: all 15 selected Python test scripts and both PowerShell camera tests pass. live_flow: 81 tests; camera input/permission: 692 checks; runner trace/ReShade/session: 29 checks.
- All 17 active jobs across 7 folders audited against main, frozen and non-frozen. Frozen: 0 failures on both. Non-frozen: the same 5 failing jobs and identical problem lists; no new failures.
- Existing non-frozen failures: pt-dialogue3, pt-s8, su1-ultra-s8-fg2, wf9-s8-compose-shift, wsf1-s8-interface-column. Historical replay evidence remains unsatisfied for all 17; no live qualification claimed.
- Logs, corpus caches and comparison reports: .scratch/queue-tooling/. Full build -Test already passes per coordinator; not rerun. Shader request delta: 0. No game launch, captures/queue writes or Git writes. external/ junction untouched.
