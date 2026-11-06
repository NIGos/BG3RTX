# Round 3: four existing S8 lives, startup blocking versus warmed x3

Audit only, against source HEAD `da6ffdf8`. All four main-checkout log files
and their runner files were read in full and scanned for cadence, state changes,
pace, wait, GPU, pipeline, swapchain and settings events. No game launch, queue
write or source change was made by this audit. Paths below are relative to
`C:/Users/quali/Documents/Code Projects/BG3RTX/captures/queue/logs/`.

| Alias | Log basename | Lines | SHA-256 |
|---|---|---:|---|
| early off | fx1-s8-fgx3-metering0.1.162410.log | 9673 | 3800e25c8c20062891672d7a781b5d887490ccb0ae7d6b9dd7882916d4a589cd |
| early on | fx2-s8-fgx3-metering1.1.162704.log | 8200 | fa8ab845b72aa84ddc910b670e4d97a00b23fd6f755da3e45ccb69d0c1abef95 |
| later off | fx1-s8-fgx3-metering0.1.163530.log | 10043 | c6657aa3cfb0d822fb96ca9ae5399751d3d20855397af63160dbf0de6d3c35d4 |
| later on | fx2-s8-fgx3-metering1.1.163841.log | 10148 | 3fcc98093c0d7eedfca25a0e174053bba33aa58aa8fe5d4dd1bfe46ec957925d |

Their corresponding `.runner.txt` files have 51, 49, 68 and 68 lines.
All use Water Queens House - 100h 13m, `visible_unfocused_fg`, real presents,
PostMessage input and no observed foreground activation (runner :5,:9).
Early off/on abort at runner :22/:20 on stale pipeline-readiness expectations,
not a renderer crash. Later off/on finish nine phases (runner :39).
The early lives remain usable evidence for the startup observable despite
being invalid for the intended complete x2/x3 matrix.

## 1. Proven blocking episode inside the SDK present call, initially at x2

All four first FG enables request **x2**. Each exhibits a long startup interval
where the CPU is inside the SDK present proxy, with SDK ratio near 2 but proxy
present-call ratio near 1. These events precede the warmed x3 measurements.
They are not explained by x3 settings, destroy waits, GetState or ReflexSleep.
The internal reason for the SDK call blocking is not observable from these logs:
it can include pacing, queued GPU work or driver waits, not just host computation.
The proxy timer also includes our downstream present hook and the raw driver's
present call (`streamline.cpp:2858-2860` at the audited HEAD). It does not
separate an SDK-internal wait from those calls.

| Run | First SDK enable | Representative slow cadence reports | Corresponding rendered pace |
|---|---|---|---|
| early off | :3874, 16:25:19.753, generate 1 | :4129 at 16:25:23.540 proxy mean/max 34.347/72.940 ms; :4401 at 16:25:31.147 44.330/55.875; :4663 at 16:25:37.776 46.333/64.549 | :4131 median 58.65 ms, :4403 63.48, :4665 63.66; p99 92.11/79.66/85.05 |
| early on | :2335, 16:27:45.101, generate 1 | :2509 at 16:27:48.234 38.995/82.026; :2920 at 16:27:59.611 72.099/127.617; :3198 at 16:28:06.389 23.210/133.546 | :2511 median 55.28 ms; :2922 95.17, p99 142.21; :3065 median 58.94 |
| later off | :1997, 16:36:20.123, generate 1 | :2218 at 16:36:25.282 40.341/61.286; :2616 at 16:36:33.952 41.985/71.148 | :2220 median 68.10 ms, p99 99.64; :2618 median 48.14, p99 92.14 |
| later on | :2010, 16:39:17.832, generate 1 | :2389 at 16:39:28.537 99.885/138.506; :2544 at 16:39:35.511 96.409/120.145; :2869 at 16:39:50.328 108.001/130.764 | :2391 median 124.93 ms; :2546 120.96; :2871 128.47 (7.8 fps), p99 151.74 |

These representative windows have **zero destroy waits**, query failures and
focus notices, successful tags/constants, GetState mean 0.000 ms / max at most
0.002 ms, and ReflexSleep mean approximately 0.016–0.026 ms. This rules out
those measured operations as the source of the tens-of-ms present-call time
in these windows. It does not rule them out in the user's earlier release run.
The largest later-on proxy maximum is 148.704 ms at :2707 (16:39:42.659).

SDK >100 ms timer-reset counts over the entire four runs are 1, 40, 2 and 122,
respectively. The later-on burst is :2024–2939, 16:39:17.962–16:39:52.915.
Thus later-on's warmed p99 alone hides a severe earlier slowdown in that run.
This is stronger evidence against retaining metering as a fix than the warmed
windows alone. Cross-launch variance prevents assigning every difference to it.

At :4125/:4257 in early off and :2383/:2539 in later on, the adjacent watch
windows report **zero pipelines, zero driver pipeline ms and zero shader
modules** while proxyPresent remains slow. Earlier loading does create many
pipelines, but ongoing creation is not the direct explanation for these
particular slow windows. `[watch] submit CPU 0.000` is **not exonerating evidence**:
`layer/perf_submit.h:8-10` requires diagnostics plus `cpu_timing=1`, default 0.
Those zero counters can mean the timer was disabled.

GPU timing is also materially inflated during the slow startup phase. These
are separately logged intervals near the slow cadence samples, not a common
per-frame partition. **Do not sum them**: averages/medians and sample windows
differ, and some pass intervals include other work. Timestamp elapsed time can
include GPU contention/preemption, rather than pure kernel execution time.
No dedicated FG-generation GPU interval is present.

| Run | Nearby GPU reports during slow phase |
|---|---|
| early off | :4315 RT passes 38.439 ms; :4336 GI 45.922 ms; :4318 reflections 3.351 ms; :4333 scene total 1.251 ms; :4311 NR 6.295 ms; :4387 RR 2.595 ms |
| early on | :2885 RT passes 40.004 ms; :2837 GI 40.069 ms; :2889 local shadows 4.167 ms; :2890 reflections 3.870 ms; :2852 scene total 1.249 ms; :2829 NR 6.338 ms; :2905 RR 2.513 ms |
| later off | :2546 RT passes 18.200 ms; :2524 GI 23.822 ms; :2573 local shadows 2.703 ms; :2549 reflections 2.381 ms; :2505 NR 6.169 ms; :2602 RR 2.474 ms |
| later on | :2808 RT passes 88.071 ms; :2811 local shadows 13.836 ms; :2812 reflections 6.498 ms; :2908 NR 6.659 ms; :2854 RR 2.726 ms |

These large GPU intervals make queued GPU completion a live explanation for
the long CPU present calls. The existing evidence cannot distinguish pacing
from GPU pressure or determine which causes which. A description of a proven
independent “SDK CPU-pacer stall” would overstate the measurement.

Immediately around the first enable, there is also a finite pipeline-creation
burst: early off :3930 has 628 pipelines / 293 ms cumulative driver time;
early on :2284 643 / 39 ms; later off :2066 616 / 51 ms; later on :2068
620 / 37 ms. Those are cumulative CPU driver receipts across a watch window,
not GPU costs or one synchronous frame stall. The later zero-creation windows
listed above show the long present calls persist after the burst.

## 2. Loading-stage transition and delayed proxy-ratio convergence

Each long first-enable episode ends around loss of valid current MV/UI and a
host Off, before the runner declares the final scene ready. Early off disables
at :4764, 16:25:39.203, stale mask 0xa; later off :2929, 16:36:39.340; later on
:3003, 16:39:53.478. NR before-post skip messages occur around the same transition.
This identifies a loading/post-chain discontinuity, not proof NR causes blocking.
NR feature creation precedes each first enable (early off :3581, early on :1930,
later off :1625, later on :1642). There is no NR-off control here.

After the second enable, CPU present time drops to approximately 0.16–0.37 ms,
but SDK ratio 2 and proxy ratio 1 persist for another 10–14 seconds:

| Run | Second enable | Low CPU, SDK 2 / proxy 1 examples | First subsequent proxy 2 report |
|---|---|---|---|
| early off | :5855, 16:25:56.464 | :6213–6897, 16:25:59.650–16:26:09.828, proxy means 0.217–0.370 ms | :7185, 16:26:13.825 |
| early on | :4425, 16:28:25.634 | :4648–5347, 16:28:27.778–16:28:37.895, means 0.184–0.358 | :5620, 16:28:41.895 |
| later off | :3907, 16:36:53.665 | :4306–4720, 16:36:57.546–16:37:03.548, means 0.163–0.256 | :5017, 16:37:07.713 |
| later on | :3991, 16:40:08.347 | :4403–4817, 16:40:12.480–16:40:18.482, means 0.167–0.192 | :5108, 16:40:22.614 |

No swapchain recreation occurs during these episodes: every run has only the
two startup swapchain-created lines (early off :458/:466, early on :459/:467,
later off :458/:467, later on :459/:467), all before first FG enable. Each
records game and proxy FIFO present mode 2, minImageCount 3 ->6. FIFO is a
possible indirect pacing constraint; this alone does not establish the display
refresh, queue depth or scanout rate.

The proxy hook increments once per intercepted `vkQueuePresentKHR`, not by
`numFramesPerBatch` (`layer/streamline.cpp:218-230` at `da6ffdf8`). Public
metering describes N consecutive separate presents. The observed counter
disagreement is real and must remain explicit; neither counter alone proves
actual scanout. The private SDK's reason for delayed extra proxy calls is unknown.

The SDK emits `Engaging WAR4639162` once just before each first enable:
early off :3866 (16:25:19.738), early on :2326 (16:27:45.040), later off
:1985 (16:36:20.027), later on :2003 (16:39:17.799). There is no public
description of that workaround in the inspected integration documents; no
causal conclusion or binary modification follows from its name. All four logs
contain **zero** CPU-pacer timeout/fallback messages, explicit full-screen-menu
decisions, common-constants-invalid messages and SDK window-not-focused notices.
Menu detection remains enabled by the normal options flag, but there is no
logged detection decision to attribute this phase to it.

FC “good” does not uniformly identify the recovery: early off has initial good
at :572 before the stall, feedback 1 at :5685 before second enable, and good
at :6960 just before proxy output converges to 2. Later off has good at :3497
before second enable, yet still SDK 2 / proxy 1 afterward. Later on has repeated
feedback/good pairs while host FG is Off (:3282/:3355, :3426/:3499,
:3608/:3682, :3758/:3856), then second enable :3991 and another ~14 s until
proxy ratio 2. Feedback is useful timing context, not a proved recovery trigger.

## 3. Warmed rotating x2/x3 windows

Plans request 30 settling and 120 measured rendered frames, with stats reset,
no PNG captures. The table below uses the **last complete logged 60-frame pace
report per phase**, not a pooled per-frame p99. Cadence denominators vary because
their reporting/reset boundaries differ; the log records exact counts.
All rows have zero destroy waits and zero query failures, with tags/constants OK.

| Run | Phase | Pace median / p99 ms | SDK / proxy ratio | Proxy CPU mean / max ms | NR GPU ms | Pace / cadence source lines |
|---|---|---|---|---|---|---|
| later off | x2 standard | 33.32 / 37.11 | 2 / 2 | 0.227 / 0.358 | 6.438 | 8069 / 8088 |
| later off | x3 standard | 33.66 / 36.20 | 3 / 3 | 0.265 / 1.818 | 6.513 | 8411 / 8430 |
| later off | x3 exact | 33.27 / 35.93 | 3 / 3 | 0.237 / 0.360 | 6.511 | 8728 / 8747 |
| later off | x2 exact | 33.43 / 35.08 | 2 / 2 | 0.297 / 0.396 | 6.233 | 9049 / 9068 |
| later off | x3 exact repeat | 33.33 / 35.33 | 3 / 3 | 0.217 / 0.331 | 6.423 | 9514 / 9533 |
| later off | x3 standard repeat | 33.25 / 37.52 | 3 / 3 | 0.274 / 0.446 | 6.603 | 9974 / 9993 |
| later on | x2 standard | 33.37 / 36.01 | 2 / 2 | 0.262 / 0.446 | 6.590 | 8122 / 8141 |
| later on | x3 standard | 33.40 / 38.21 | 3 / 3 | 2.800 / 24.575 | 7.021 | 8455 / 8474 |
| later on | x3 exact | 33.29 / 39.29 | 3 / 3 | 4.438 / 25.525 | 7.117 | 8780 / 8799 |
| later on | x2 exact | 33.27 / 37.26 | 2 / 2 | 0.299 / 2.518 | 6.394 | 9111 / 9130 |
| later on | x3 exact repeat | 33.44 / 36.02 | 3 / 3 | 0.231 / 0.390 | 6.460 | 9577 / 9596 |
| later on | x3 standard repeat | 33.19 / 36.40 | 3 / 3 | 0.292 / 0.385 | 6.421 | 10056 / 10075 |

NR lines follow their listed pace line by four lines. The excerpted final
rows do not replace all-window gates: earlier reports can contain higher maxima.
Metering off/on support logs are later off :101 and later on :108. On queries
and enables both extension and feature, proving the candidate actually applies.
Nevertheless it provides no warmed ratio gain and introduces longer x3 proxy
calls in several windows, plus the much worse startup episode. Rejection is
supported; there is no reason to promote it from these results.

Both later runs have two SDK eOn pause/resume pairs in static **x2** warmup,
without focus notices: later off :6336/6344 and :6403/6410, later on
:7496/7504 and :7588/7594. They occur near FC-feedback changes. The warmed
measured rotating x3 windows contain no new SDK pause. This is conditional
functional x3 success at approximately 30 rendered fps, not a reproduction or
fix of the user's foreground, moving, uncapped rendering problem.

## 4. What caps the warmed base at 30 fps?

All four logs explicitly set `fps_cap=0`, report `[framecap] off`, and send
Reflex frame limit 0. Examples: early off :3/:449/:482; later off :3/:449/:483;
later on :3/:450/:483. `layer/framecap.cpp:19-21` returns without waiting for
cap <=0 or Streamline ownership. `streamline.cpp:1057-1066` passes the zero cap
unchanged. Measured ReflexSleep is about 0.02 ms, not 33 ms. Thus neither of
our explicit limiter routes explains the 30 fps bound in these lives.

The 33.3 ms cadence already exists in loading while host FG is Off (early off
:626/:756, later off pre-enable pace reports), and returns in both x2 and x3.
Runners explicitly keep the window unfocused; later on has four occluded checks
(runner :11 onward), whereas later off has zero. Background focus-hook logs
prove spoofing is active; they do not prove every engine/driver/DWM pacing
decision treats the window as foreground. An unfocused/game/display limit is
therefore plausible, but its exact source is **not proved**. PostMessage itself
is an input method, not a demonstrated frame limiter. No measured GPU-to-CPU
queue timeline isolates FIFO, engine inactivity logic or display composition.

Release indirect-limit work should preserve this distinction: the initial
SDK-present blocking is directly measured and severe even with metering Off;
the warmed 30 fps ceiling is external to our two explicit cap routes; x3's
uncapped throughput and actual displayed-frame cadence remain unqualified.
