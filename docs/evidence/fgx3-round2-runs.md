# Round 2 run inventory and enabled-only x3 evidence

Read-only sources: main checkout `captures/queue/logs/*.log` (220 files,
519,867,503 bytes), all 1,024 rows of `captures/queue/results.jsonl`,
TRACK_B_FG history, progress fragments and FG evidence files. The requested
`test-main/captures/queue` directory does not exist. Recursive log search also
checked archived batch slices for x3/x4 markers. No game or queue mutation occurred.

[Compact run inventory](fgx3-round2-runs.csv) indexes **every one of the
220 mode-bearing raw logs**, their SHA-256, associated result-row line number,
settings modes, state/proxy ratios, watch-derived rendered fps, GPU message
summaries, rendered frame-time percentiles, SDK pause notices and destroy waits.
The full extraction, with GPU message first/last samples and all summary
denominators, is kept outside Git at
`C:/Users/quali/Documents/Code Projects/BG3RTX/.scratch/coord/fgx3/round2-runs-full.json`.
It also indexes 193 result rows with an explicit mode marker; other result rows
do not disclose mode. Rows without a marker cannot be classified as x2 or x3.
All 220 raw logs contain x2 markers. No raw log/result row contains x3 or x4
markers in any searched spelling (`fg=3/4`, `fg:3/4`, `fg 3/4`, JSON numeric/string
values, `frame_generation=3x/4x`, `requested 3/4`, `frames to generate 2/3`,
`numFramesToGenerate=2/3`, or `gen=2/3`). This is an inventory of retained evidence,
not proof no other live ever happened.

## A. Scene comparison and qualification

No retained queue run provides a controlled x2/x3 comparison in any scene.
The following scene buckets use log/job names; mixed combined jobs and unnamed
logs cannot establish a particular save or camera. Their numerical ranges are
**ranges of whole-run medians**, including startup, deliberate toggles and PNG
capture stalls. They are an inventory, not acceptance measurements. SDK/proxy
ratios are integration counters, not scanout; historical periodic SDK per-query
counts also have the double-query limitation repaired by round 1.
CSV GPU line references identify RR, scene build, GI, local shadows, reflections,
input copy and NR messages when present; source messages contain the complete
component timings. Missing fields mean unavailable, not zero. CSV pace summaries
are medians of logged window medians/p99s, not reconstructed frame distributions.

| Scene/name bucket | Retained x2 logs | x2 rendered fps: whole-run median range | x2 proxy ratio: whole-run median range | Retained x3/x4 queue evidence | Controlled x3 conclusion |
|---|---:|---:|---:|---|---|
| Water Queens House / S8 | 69 | 23.61–30.00 | 1.00–2.00 | none | unqualified |
| Camp | 26 | 19.80–29.99 | 1.00–2.00 | none | unqualified |
| Inn / Mermaid | 41 | 24.60–29.99 | 1.00–2.00 | none | unqualified |
| Dialogue | 38 | 0.00–30.00 | 1.00–2.00 | none | unqualified |
| Other / unassigned | 46 | 18.29–36.00 | 1.00–2.00 | none | unqualified |
| User session, unnamed moving scene, Ultra + NR | supplied logs, not queue | enabled x2 27.31–33.02 in sampled subintervals | absent | enabled x3 mostly 21.48–30.93; startup interval 45.88 | actual x3 symptoms remain unexplained |
| 2026-10-26 historical TV-standby runs, scene unnamed | TRACK summary only | 2.4–3.2 | 0.50–1.00 from reported counts | x3 2.2–2.6 rendered fps; SDK count 3; x4 2.2–2.6, count 4 | functional/stability only, dropped frames, no smoothness qualification |
| 2026-10-26 historical live 120 Hz, scene unnamed | TRACK summary only | 59.8–60.2 | about 2.00 | x4 39.8–40.0 rendered, proxy ratio 3.00, SDK count 4; x3 absent | x4 functionality only; not x3 qualification |

The last two rows are TRACK_B_FG.md:427–459 (same assertions already present
in commit `77325159`). The earlier statement “works ... at 2x, 3x and 4x”
at :231 is broader than its deciding evidence: x3 is recorded with the TV
in standby, dropped frames and CPU-pacer timeout warnings. The only live-display
table measures x2 and x4; it does not measure x3. Neither table establishes
modern Ultra + NR x3 cadence, x3 quality or x3 GPU cost. Its interpretation of
the x4 proxy as display output is historical; proxy ratio 3 and SDK count 4
cannot alone distinguish display refresh limiting from integration accounting.

For x2, `docs/evidence/p1-fg-live-20261029.json` records static parity at
18.7–18.8 rendered fps, proxy ratio 2, and 99.5195–99.5215% primary match, but
its motion matrix is invalid. FG-copy evidence later records stable x2 ratio 2
with preserved-input correctness and 0.026 ms GPU copy; the motion-image metrics
retain documented capture/cadence limits. Those are x2-specific qualifications.

A directly relevant retained x2 + NR run is
`MAIN/captures/queue/logs/dlss5-s8-ultra-native.1.154638.log`, results.jsonl row
linked by the JSON inventory, marked valid. Across the **whole run**, median
watch-derived fps is 30.00 and median proxy ratio 2.00 (startup minimum 0.98).
Its 96 rendered pace reports have median-of-medians 33.36 ms and median-of-p99s
38.195 ms; worst p99 is 2488.48 ms at :14130, 15:50:15.843. This is not an
isolated steady-state p99. Last GPU logs: :14425 GI 11.672 ms, :14434 local
shadows 0.686 ms, :14435 reflections 1.216 ms, :14412 scene total 1.180 ms,
:14403 RR total 2.452 ms, :14357 FG input copy 0.026 ms. NR has 34 reports,
median 6.472 ms (range 6.222–6.897); :14134 gives 6.654 ms. There are five
SDK >100 ms resets and 30 destroy-wait messages, without wait durations.
**FG generation GPU cost is absent**; input-copy time is not generation time.

## User report: x3 while SDK interpolation is enabled

The user clarifies they stayed in game except for launcher FG changes. SDK
focus notices remain real evidence, but **focus loss does not explain the
reported poor x3 cadence inside enabled periods**. The following analysis
explicitly removes disabled periods. Enabled boundaries come from sl.log's
microsecond monotonic timestamps, aligned with layer timestamps using the
shared warning at sl.log:607 / bg3rtx.log:890 (offset 15:03:28.396, rounded
to 1 ms). Samples at boundaries are excluded unless wholly inside. Mode changes
that preserve SDK enabled state split intervals at the layer FG decision.

Each fps estimate uses the first and last 30-frame input-copy counter sample
inside that interval; no absent per-frame timings are reconstructed. The first
enabled interval overlaps the post-chain/scene discontinuity, so its 45.88 fps
must not be treated as the steady scene rate. Very short intervals have too few
samples for fps. “Waits” counts successful wait calls, not blocking milliseconds.

| SDK-enabled interval (layer clock) | Mode | Sample frame delta / sample seconds | Rendered fps | Destroy waits | >100 ms resets | Source lines: sl.log enable/disable; bg3rtx.log sample endpoints |
|---|---:|---:|---:|---:|---:|---|
| 15:04:10.589–15:04:36.935 | x3 | 1110 / 24.194 | 45.88 | 54 | 3 inside; 1 at enable boundary | 606/621; 943/1131 |
| 15:05:01.139–15:05:16.542 | x3 | 330 / 13.842 | 23.84 | 0 | 1 | 623/626; 1982/2032 |
| 15:05:33.448–15:05:47.279 | x3 | 300 / 13.039 | 23.01 | 108 | 1 | 627/630; 2065/2269 |
| 15:05:49.372–15:06:02.765 | x3 | 300 / 12.273 | 24.45 | 18 | 0 | 631/634; 2275/2327 |
| 15:06:03.901–15:06:05.317 | x3 | one sample only | unavailable | 0 | 0 | 635/637; 2360 |
| 15:06:39.985–15:06:40.498 | x3 | one sample only | unavailable | 0 | 1 | 639/642; 2422 |
| 15:06:40.668–15:06:53.452 | x3 | 240 / 10.630 | 22.58 | 105 | 0 | 643/645; 2424/2626 |
| 15:07:07.740–15:07:12.061 | x3 | 60 / 2.513 | 23.88 | 0 | 0 | 646/648; 2764/2771 |
| 15:07:12.105–15:07:34.117 | x3 | 450 / 20.947 | **21.48** | **276** | **7** | 649/658; 2817/3331 |
| 15:07:58.204–15:08:17.507 | x3 | 540 / 17.460 | 30.93 | 162 | 0 | 659/661; 3469/3752 |
| 15:08:30.805–15:08:35.735 | x3 | 90 / 3.608 | 24.94 | 0 | 0 | 662; 3777/3786 |
| 15:08:35.735–15:09:04.996 | x2 | 810 / 28.161 | 28.76 | 290 | 0 | 662/665; 3796/4372 |
| 15:09:06.270–15:09:09.122 | x2 | 60 / 2.197 | 27.31 | 18 | 0 | 666; 4379/4412 |
| 15:09:09.122–15:09:12.791 | x3 | 60 / 2.527 | 23.74 | 18 | 0 | 666/668; 4424/4432 |
| 15:09:19.512–15:09:25.367 | x3 | 120 / 5.003 | 23.99 | 36 | 0 | 669/671; 4470/4534 |
| 15:09:25.976–15:09:28.926 | x3 | 30 / 1.119 | 26.81 | 0 | 0 | 672; 4539/4542 |
| 15:09:28.926–15:10:40.215 | x2 | 2340 / 70.859 | 33.02 | 384 | 0 | 672/end; 4557/5469 |

The seven enabled-x3 reset lines are bg3rtx.log:3135 (15:07:20.895), :3137
(15:07:21.482), :3149 (15:07:21.706), :3166 (15:07:22.517), :3167
(15:07:22.871), :3168 (15:07:23.308), :3169 (15:07:23.500). SDK remains
enabled throughout and the previous cap was already removed (:2770 at
15:07:10.572). This establishes long-frame gaps beyond launcher/focus toggles.
The main supplied session has **no GPU-ms or rendered frame-time percentile
reports** (detailed_logging off), and no wait duration. No direct generation
cost, GPU bottleneck, wait serialization or p99 can be derived from these gaps.

The last neighboring enabled x2/x3/x2 sections show 27.31 -> roughly 23.74–26.81
->33.02 rendered fps while NR remains enabled. This is evidence consistent with
extra x3 work/pacing cost, but movement, camera, scene demand and sampling spans
are uncontrolled. There is no scanout ratio, frame-pair timing or FG GPU timer
to prove proportionality or isolate the stutter. The current evidence therefore
does **not qualify x3**, and round 1's diagnostic/API fixes do not resolve it.
