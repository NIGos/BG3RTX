# CPU hook optimization 2

Offline preparation only. `cpu_opt2=0` by default, layer-only. No queue staging,
game launch or commit. ABI and shader requests unchanged (request delta 0).

## Existing receipts and priorities

Reused the CPU1 normalized parser, with material sub-timers added by `analyze.py`.
Full normalized receipts: [005111](profile-005111.md), [CPU1 025350](profile-cpu1.md).
The older complete profile in `../cpu/profile-inn.md` remains separate evidence;
its 12.623/15.395 ms hook totals must not be substituted for the requested 005111.

| CPU scope | 005111 Ultra static | 005111 Ultra pan | CPU1 025350 OFF a / OFF b |
|---|---:|---:|---:|
| Capture hooks, inclusive | 17.760 | 23.028 | 12.686 / 12.665 |
| Pre-draw, inclusive | 11.467 | 12.820 | 8.696 / 8.624 |
| Pass after, includes scene recording | 4.400 | 7.827 | 2.550 / 2.636 |
| G-buffer material hooks, nested | 2.777 | 3.838 | 2.064 / 2.032 |
| Hit material prepass, nested | 1.901 | 1.980 | 1.569 / 1.569 |
| Draw bookkeeping | 0.641 | 0.648 | 0.525 / 0.526 |
| Descriptor tracking, excludes driver | 0.015 | 0.817 | 0.0028 / 0.0030 |
| Alpha clone driver updates, nested | 0.783 | 0.838 | 0.512 / 0.513 |
| Submit, includes driver | 1.540 | 1.963 | 1.064 / 1.059 |
| BVH input keys (ms/build), nested | 1.127 | 1.947 (one receipt) | 0.490 / 0.528 |
| TLAS instances (ms/build), nested | 0.930 | 1.030 (one receipt) | 0.638 / 0.704 |
| Alpha record commit (ms/build), nested | 0.284 (one receipt) | unavailable | 0.175 / 0.172 (one each) |

Numbers are ms/frame unless labelled ms/build. Watch CPU totals are divided by
the same line's rendered `presents`, never FG `displayed`; integer microseconds
truncate each SubmitCpuTimer call. All-thread sums are not critical-path time.
Nested scopes overlap: do not sum these rows. CPU1 OFF/ON/OFF/ON is one run;
cross-run differences above are context, never an optimization delta.

The top three actionable scopes are pre-draw, scene recording within pass-after,
and G-buffer material hooks nested within capture. New `capture detail` QPC
scopes separate pipeline lookup, cascade identity, BVH classification, alpha
records, material hooks and dispatch pre/post tracking (driver excluded). They
are log-only, require detailed logging plus
cpu_timing, and retain the existing 120-frame override/reset convention.
Dispatch tracking was not separately timed in these historical logs; the hook
timer addition makes that previously missing category observable.

Life submit bookkeeping is minute-wide context: 005111 reports 0.020/0.089/0.049
ms/frame over 1397/1620/1640 frames; CPU1 025350 reports 0.023/0.062 over
1538/1710. These include mutex/map work at submit, not all fence polling and
destruction; they cannot be assigned to individual short windows.

005111's High windows and pan's scene sub-timers are insufficient, not passes.
CPU1 024409 timed out on a nonexistent cpu_opt1 knob witness; 025003 timed out
on a settings-read witness in ready-off-a. Their loading/warmup numbers are
confounded and not A/B evidence. The later 025350 run completed all nine phases;
its CPU1 evidence is retained above, and is not a CPU2 test.

## Exact work reductions

Under CPU2, alpha snapshots stage lifetime handles on the stack and read
`rt_dyn_hold` from the existing hot settings table. Every original tracker lookup,
view-image lookup and lifetime extension remains. Capture-local alpha
texture tables preserve the first matching slot with bounded membership tables.
G-buffer material hooks reuse the early qualification result when the instancing
policy still matches. Material queries build a first-record index once and use
fixed small membership storage instead of repeated scans and heap allocations.
Mesh eviction ranges stay in stack storage until a new extraction is confirmed;
cached draws avoid the temporary vector allocation. TLAS builds reuse the slot's cached size receipt, removing one
driver size query per build. Native draws, submit calls, scene contents and record
order are preserved by the changed CPU algorithms. Offline fixtures check the
affected CPU decisions, records and ordering; they do not replay a full Vulkan
scene. Live savings remain unmeasured.

## Prepared decision and gates

One inn Ultra session uses OFF/ON/OFF/ON, 600 rendered frames per window, with
`cpu_opt1=0` fixed to isolate CPU2. Warm the ON storage before the first OFF.
The ready phases wait only for a fresh zero-pipeline watchdog. No invented
pipeline completion or custom cfg-key/settings-read witness is required.
The runner still performs its normal settings acknowledgement.

`expect_every` requires submit, capture aggregate, capture detail, material and
BVH receipts in every measured window. The post analyzer also requires every
fine capture/dispatch scope, material key/mutex/collect receipts and both scene phases
in all four windows (at least two complete periods each). Zero CPU is valid for
a fine scope. The inherited watchdog must have nonzero submit CPU.
The 300-build alpha/scene/matrix receipts drop the first straddling receipt and
remain contextual if only one wholly contained receipt survives. The 120-frame
alpha-detail timer supplies the per-window alpha CPU gate.

Compare each ON median against both OFF windows and their observed spread.
Claim a saving only when both ON deltas exceed that repeat spread. Fixtures prove
the affected CPU decisions, records and ordering; full Vulkan scene replay
equivalence is not tested. This live job is a cost receipt,
not a visual or energy qualification. No CPU2 live saving is claimed.

Generate: `python tools/live_batches/opt_r3/cpu2/make_job.py`.
Lint: `python tools/live_batches/live_flow.py tools/live_batches/opt_r3/cpu2 --frozen --report tools/live_batches/opt_r3/cpu2/lint.json`.
Lint passes against the frozen 79-log corpus. Only three new readiness queries
were appended to its snapshot; existing receipts were preserved. Readiness is
explicitly unproven. Frame windows take about 205 s at the frozen same-save
11.69 fps p10, plus warmup/readiness; the 300 s cap remains decision-critical.
