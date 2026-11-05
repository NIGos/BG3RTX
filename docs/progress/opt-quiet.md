## Quiet-only pipeline readiness (branch opt-quiet, kept offline, 2026-11-05)

- Shader-changing keys now require only a fresh QUIET watch record after settings acknowledgement. No generated family/request completion guesses; layer-only and push-constant keys still need no pipeline wait. Unknown keys still require an explicit contract.
- Regenerated gi_roof, scene_holds, gi_attr, release_qual/final and opt_profile through their generators, including release intermediates. The final High diagnostic transition also uses fresh QUIET instead of an inherited generation witness.
- All 9 requested jobs pass live_flow frozen lint. Added 38 missing readiness queries only; every prior snapshot record, regex match and timing model is preserved. New queries remain explicitly unproven.
- Tests: batch_jobs_lint 12/12, live_flow 72/72, release_qual 5/5, merge_scope 9/9. Readiness assertions require QUIET only and reject missing freshness.
- Shader request delta 0; no rendering change or GPU cost claim. No game launch, queue staging or commit. Live confirmation remains pending explicit user steering; the existing inn A/B jobs should proceed past shader transitions without guessed-request timeouts.
