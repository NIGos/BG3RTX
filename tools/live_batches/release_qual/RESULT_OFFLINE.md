# Lean release offline receipt, 2026-11-04

Status: READY_FOR_LIVE preparation only. No game launched or queue inbox written.

| Job | Expected minutes including 120 s load | Captures |
| --- | --- | --- |
| rel1-smoke-ultra-s8 | 3.96 | None |
| rel3-stock-hair-dialogue1 | 4.76 | One static PNG for visible cards and face/neck shadows |

High was dropped: it keeps OMM/masks off. Ultra retains static, Q/E pans,
both rotations, capacity/scene-wait/budget/RT-water gates and one master OFF/ON
cycle. The twice-qualified OMM same-ray diagnostic and smoke captures are removed.

Regenerated with --profile-exe "$env:TEMP/bg3rtx-release-profile.exe", the
existing exporter path documented in README.md. Rendering overrides remain empty.
Shared live_flow preflight passes both jobs; batch_jobs_lint_test.py passes 6/6.
The older synthetic dryrun receipts predate this edit and were not rerun.

The 12:19 rel3 live failed on the occ-stats expectation while its diagnostic
key was off. Removed that expectation, both occ diagnostic phases and the
face/neck counter gate. The remaining gates and single static PNG are retained.
Total expected time is 8.72 minutes including both 120 s load allowances.

Historical replay evidence is incomplete and some readiness transitions have no
same-save/key timing samples. Estimates are not hard runtime guarantees or live
qualification. The dialogue PNG still needs the user's visual check.
