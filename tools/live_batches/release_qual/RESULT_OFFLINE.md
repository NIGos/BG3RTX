# Offline preparation receipt, 2026-11-04

Status: READY_FOR_LIVE preparation only. No game launched and no real pending
inbox written. No live authorization, rendering quality pass or release approval.

| Job | live_flow preflight | lean_dryrun negative / positive | Expected minutes |
| --- | --- | --- | --- |
| rel1-smoke-ultra-s8 | PASS | INVALID as expected / valid completed | 6.97 |
| rel2-smoke-high-s8 | PASS | INVALID as expected / valid completed | 6.97 |
| rel3-stock-hair-dialogue1 | PASS | INVALID as expected / valid completed | 5.41 |

All three pass shared camera sampling, diagnostic roles, scene-wait and capacity
lints, trace markers, 512 MB batch budgets (130 MB each), unfocused mode and
15-minute launch limits. `batch_jobs_lint_test.py`: 6/6 existing regression tests
pass; the new prepared jobs are checked individually by live_flow/lean_dryrun,
not added to the active authorized inventory. `git diff --check` passes.

Profile comparison: zero rendering differences from the exported launcher Ultra
in rel1/rel3. Rel2 differs only in dev=1 plus the four documented OMM/mask keys.
The run-wide strand exclusion gate accepts no strand producer and rejects a
synthetic primary-publish line, including before measured windows.

Final shared dryrun completed at 10:12:45 local time: each job's negative and
positive fake-queue paths passed. Scratch roots and full outputs are in TEMP;
the final output is `bg3rtx-release-qual-final-dryrun.txt`. An earlier concurrent
regeneration copied a transient timeout into the synthetic High job; freezing
the generated files and rerunning all jobs resolved it.
Dialogue was rerun after requiring a fresh startup watch and retaining the 180 s
cold-build allowance; both fake-queue paths pass in
`bg3rtx-release-dialogue-final.txt` in TEMP. Its final estimate is 5.41 minutes.

Historical replay has missing evidence for all three jobs. Readiness estimates
also retain unproven same-save/key transitions. Neither is a live failure or a
qualification receipt. High policy promotion/final dev=0 confirmation and the
dialogue PNG visual checks remain required, as documented in README.md.
