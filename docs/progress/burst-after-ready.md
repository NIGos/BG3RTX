## Burst after readiness (branch burst-after-ready, kept offline, 2026-11-04)
- Defer explicit pan_capture settings until motion-ready; hv6 replay verifies readiness precedes arming. Event windows and plans unchanged; no live run.
- Camera regression, queue gates and live-flow tests pass. Active lint: 18/26 pass, including hv6; 8 existing S8 cadence failures recorded in .offline/burst-after-ready-active-lint.json.
