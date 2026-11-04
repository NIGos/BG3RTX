# Non-hair backlog run order — prepared 2026-11-04

No live authorization. All artifacts remain outside watched queue inboxes.
Minutes include 120 s loading and measured expected readiness where available.
Cold readiness without a VALID witness is unproven and fails closed at its cap.
Run stock hair, visible_unfocused_fg. Never build/compile during cost windows.

| Order | Scene | Job | Expected minutes | Full cold bound minutes |
|---|---|---|---:|---:|
| 1 | S8 | reflections_offscreen/wos1-s8-fg2 | 4.84 | 10.89 |
| 2 | S8 | water_offscreen/wo1-s8-hit-shade (bit 1 only) | 5.27 | 13.33 |
| 3 | S8 | water_offscreen/wo1-s8-bit2 | 5.27 | 13.33 |
| 4 | S8 | water_offscreen/wo1-s8-bit4 | 5.27 | 13.33 |
| 5 | S8 | backlog_diagnostics/as1-alpha-spill-s8 | 5.83 | 11.83 |
| 6 | S8 | backlog_diagnostics/cs1-current-sun-s8 | 4.30 | 5.30 |
| 7 | S6 / Adamantine Forge | cov_paks_compact/cp5-compact-s6-publication | 5.39 | 13.39 |
| 8 | Camp | m4_generated/hgn1-generated-camp-fg2 | 6.57 | 11.73 |
| 9 | Camp | qual_stage4/03-guards-camp | 5.50 | 10.18 |
| 10 | Inn | m4_generated/hgn2-generated-inn-fg2 | 5.39 | 10.55 |

Total about 53.6 minutes; each launch under 15 minutes even with all declared
cold waits. Never execute qual_stage4 01/02 as well as m4: they are aliases.
SHaRC has no live launch; follow ../sharc/README.md.

The three water bits deliberately use independent OFF/ON/OFF/ON/OFF controls.
Combining their complete cold-wait budgets would exceed the launch cap. Current
Sun is restart-latched ON and cannot share the water jobs' OFF configuration.
The shading job has an 18 FPS conservative ceiling versus the cost jobs' 30 FPS.
Camp generated rows and guards also exceed 15 minutes when their full cold waits
are concatenated. This order groups saves without pretending one oversized
session is lean. Historic combined_20261103 jobs are not this run order.

Read each batch predicted.json before execution. Reject failed quality/cost bits
separately, preserve every ON observation, and report INVALID for missing
participation/captures. Current Sun and GI guards are fix/reject screens only:
aggregate readiness counts cannot identify the failed shader conjunct, and PNGs
cannot prove the original five-K16 transient contract (~11 GiB). SHaRC cannot
qualify without provenance-matched cache history and PT inputs.

Before a live request, the coordinator must record these uncommitted predictions
in the candidate revision (Git metadata is read-only for this task); existing
energy post gates check that provenance. No job here qualifies itself offline.
