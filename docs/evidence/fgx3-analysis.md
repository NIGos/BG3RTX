# FG x3 session analysis (offline, 2026-11-06)

Primary evidence is the five supplied files under
`C:/Users/quali/Documents/Code Projects/BG3RTX/.scratch/coord/fgx3/`.
All files were read in full for event extraction, counts, handle matching and
classification; relevant intervals were then inspected with surrounding lines.
Log line numbers below are one-based and include blank lines. Code references
in the diagnosis refer to the pre-fix branch HEAD `531a7544` unless stated otherwise.
The user's installed release is reported as `8599711c`; no installed binary was run.

| Source | Bytes | Lines | SHA-256 |
|---|---:|---:|---|
| bg3rtx.log | 702001 | 5471 | 4ad8acbe2619ba6e15f2ec0b18dd111e026a843ed28b5f9750b2d6ac15b98de4 |
| sl.log | 84470 | 673 | 4d63518356f3e27f2be5169655390c217074eaa683e48a52dcc0d7ace8a08690 |
| bg3rtx.prev.log | 271900 | 2758 | c4248697488546e3a476b1b5db48a8ef79ece1b1cff10159a6a9983002235bd0 |
| bg3rtx_user.ini | 452 | 17 | ca3b863ebe75f60878d0d7c4bf68677a831ecbffce4a49d0a1a514d12276e3ae |
| bg3rtx.dlss5-runtime.log | 1503 | 8 | 842560d2505dcaa884cd79f0bb35de07b6738731cdedc61100547d69b93c03d4 |

## Ranked findings

1. **Proven dominant reason for repeated SDK pauses: loss of window focus.**
   Eleven of twelve enabled-to-disabled transitions with requested `eOn` have
   `shouldInterpolate: DLSS-G disabled: window not focused` immediately before
   them in sl.log. The other transition is a 169 ms interval without a named
   reason. This is independent of the host's seven options decisions.
   `SyncInterval=1` accompanies the pauses; it does not prove that VSync caused
   those pauses. With the same requested VSync, enabled periods have interval 0.
   The integration does not own the SDK's focus policy. The cause of Windows
   focus changes (user actions, overlays or another application) is not recorded.
2. **Proven broad blocking waits; stutter contribution unmeasured.**
   The destroy hooks wait on the latest global FG input fence even for historical
   watched resources with current roles 0. `layer/streamline.cpp:2347-2373`
   snapshots the global fence and calls a potentially one-second host semaphore
   wait; `:2377-2410` applies it to retired views/images.
   `layer/tracker.cpp:751,778` invokes those calls with blocking enabled on the
   original game destruction path. Retired objects have an existing deferred
   destruction route (`:752,779,1092-1129`). This policy can wait for a newer
   frame than the resource's actual final use, but the logs do not establish
   each resource's final SDK use. Optimizing retirement would require that
   ownership proof and safe use of the deferred route. The logs prove
   calls and successful return, **not elapsed wait time or lost frames in flight**.
3. **Proven diagnostic defect: the SDK presented counter was queried twice.**
   `layer/streamline.cpp:2590-2630` queries state after present for the fence and
   again periodically for logging. SDK `numFramesActuallyPresented` is drained
   by GetState, so the second query cannot represent that frame's whole count.
   This can corrupt attribution, not by itself prove actual presentation stutter.
   sl.log:521 at 15:03:34 (5.694810 s) warns that GetState must synchronize with
   present. A warning alone does not prove concurrent calls: startup and present
   messages here both name thread 10448. No unsynchronized-call race is established.
4. **Proven low rendered throughput and unsupported Vulkan FG VSync; impact split unknown.**
   Rendered rates below differ between changing scene/window conditions. They
   imply approximately 72-76 displayed fps at x3 when generation is uninterrupted,
   rather than a fixed multiplier of the higher x2 rendered rate. The SDK reports
   VSync support 0 (bg3rtx.log:127, 15:03:34.090), and sl.log:605 starts repeated
   `VSync interval 1 not supported with FG` warnings at 15:04:10; occurrence 4096
   is logged at 15:08:48 (:663). SDK ProgrammingGuideDLSS_G.md:1159 explicitly
   excludes Vulkan from FG VSync support. The warning does not identify the
   eleven focus pauses as VSync failures. No measured display-refresh ceiling,
   scanout rate, or GPU pass cost can be derived from these production logs.

## Every SDK pause and NR relationship

Times are SDK wall-clock seconds as printed; durations use its precise monotonic
timestamps. There are thirteen pause/resume pairs: twelve requested eOn and one
host eOff. Eleven focus pauses total 115.223 s; the unexplained eOn pause adds
0.169 s. Initial focus-disabled startup is additional and excluded from this sum.

| Pause/resume sl.log lines | Pause time | Duration s | Requested | Named reason |
|---|---|---:|---|---|
| 621 / 623 | 15:04:36 | 24.203470 | eOff | host stale inputs, then focus blocks resume |
| 626 / 627 | 15:05:16 | 16.906044 | eOn x3 | focus (:625) |
| 630 / 631 | 15:05:47 | 2.093066 | eOn x3 | focus (:629) |
| 634 / 635 | 15:06:02 | 1.136181 | eOn x3 | focus (:633) |
| 637 / 639 | 15:06:05 | 34.667755 | eOn x3 | focus (:636) |
| 642 / 643 | 15:06:40 | 0.169444 | eOn x3 | unspecified |
| 645 / 646 | 15:06:53 | 14.287928 | eOn x3 | focus (:644) |
| 648 / 649 | 15:07:12 | 0.043106 | eOn x3 | focus (:647) |
| 658 / 659 | 15:07:34 | 24.087241 | eOn x3 | focus (:657) |
| 661 / 662 | 15:08:17 | 13.297240 | eOn x3 | focus (:660) |
| 665 / 666 | 15:09:04 | 1.274299 | eOn x2 | focus (:664) |
| 668 / 669 | 15:09:12 | 6.721018 | eOn x3 | focus (:667) |
| 671 / 672 | 15:09:25 | 0.608728 | eOn x3 | focus (:670) |

The main layer log actually continues to 15:10:40.215, beyond the initially
described 15:09 end. It has seven FG decisions (:147,819,1135,1929,3790,4418,4547),
five stable-input decisions and two missing/stale decisions. At 15:04:36.935,
:1135 disables on stale MV/UI (mask 0xa), after NR skips at :1085 15:04:36.653;
this is a shared discontinuity, not proof NR caused it. At :1929 15:04:52.045
the host requests On again; SDK :622 still reports focus loss, then resumes
only at 15:05:01 (:623).

There are eighteen `RR before-post chain unavailable` and three `no usable
pre-post RR evaluation in recent frames` skip messages. The first reason is
logged on state changes, not every skipped frame (`layer/dlss5.cpp:39-41`);
the second tests a 120-frame gap (`:344`). Consequently these counts are not
skipped-frame counts. Many focus pauses have no nearby NR skip: e.g.
15:05:16/47 and 15:06:02/05, with preceding skip 15:04:50.361 (:1883) and next
15:06:50.335 (:2596). At 15:07:12 the SDK focus pause precedes the next skip
15:07:13.624 (:2842). At 15:09:12 and :25, the previous skip is 15:08:57.943
(:4236), 14 and 27 seconds earlier. The alleged NR-skip -> missing FG tags ->
SDK pause causal chain is therefore **not supported**.

NR rejects the unavailable post chain and returns (`layer/dlss5.cpp:264-271`);
it does not clear FG tags. FG hudless/UI discovery/publication is separate
(`layer/streamline.cpp:2208,2279,2340-2346`). bg3rtx.log:5420 at 15:10:37.969
skips NR, :5422-5423 immediately discovers new hudless/UI, :5429 15:10:38.029
resumes NR, and :5470 15:10:40.182 still reports tags/constants ok. No recurring
SDK common-constants-missing failure is recorded. Startup sl.log:538 instead
warns about optional backbuffer extent 0x0 and resets it to full dimensions.

There are fourteen SDK >100 ms timer resets (:607,609,611,614,624,628,640,
650-656). Seven cluster at 15:07:20-23 without an interpolation-state transition
until 15:07:34. They demonstrate long frame gaps but do not identify their origin.

## Destroy waits, resource identity and throughput

There are **1609** successful wait messages: 1074 views and 535 images; 917
under requested x3 and 692 under x2. These totals have unequal durations and
different scenes, so they cannot show a higher x3 frequency or longer waits.
At least 354 image waits match explicit earlier game composite handles:
178 UI and 176 hudless. The remaining 181 cannot be assigned from handle logs.
For example, :665 at 15:03:45.011 names UI `000002BD8E8CDED0`; :927 at
15:04:12.143 waits fence 712 to destroy it. :664 names hudless
`000002BD8E8CD8D0`; :934 at 15:04:12.397 waits fence 715 for its destruction.
The post-destroy reports record current roles 0. At :5431-5466, x2 already
shows ~40 ms spacing between retired view/image destructions and a newer
fence target each time. Thus this pattern is not exclusive to x3.

These are not evidence of NR slot churn: only three NR slot allocations occur
(:680 15:03:45.021, :703 .139, :708 .172), all 210.9 MiB, followed by a single
feature creation (:686). NR owns five separate private images per slot and
reuses retained recordings (`layer/dlss5.cpp:134-169`); no repeated main-session
slot allocation, feature recreation or exhaustion message occurs. Its private
images are destroyed via downstream device dispatch (:55-59), not selected as
hudless/UI. Eight is a capacity limit, not eight observed allocations.

Rendered throughput derives from elapsed layer timestamps between logged
`input copy frame` counters, not generated frames. Below uses the first/last
sample wholly inside each contiguous requested-mode interval after the loading
discontinuity. Every listed endpoint selected a fresh copied input (submit age 1).
No scene or focus equivalence is assumed.

| Mode | First timestamp/frame/line | Last timestamp/frame/line | Frames / seconds | Rendered fps |
|---|---|---|---|---:|
| x3 | 15:05:01.059 / 2790 / 1981 | 15:08:34.885 / 8190 / 3786 | 5400 / 213.826 | 25.254 |
| x2 | 15:08:36.035 / 8220 / 3796 | 15:09:08.649 / 9150 / 4412 | 930 / 32.614 | 28.515 |
| x3 | 15:09:10.948 / 9210 / 4425 | 15:09:27.090 / 9600 / 4539 | 390 / 16.142 | 24.161 |
| x2 | 15:09:29.323 / 9660 / 4557 | 15:10:38.953 / 11970 / 5460 | 2310 / 69.630 | 33.175 |

The earlier x3 startup interval 15:03:47.087 frame 390 ->15:04:35.494 frame
1770 gives 28.508 fps, but includes private-copy exhaustion while unfocused:
:858-866 report eight unavailable slots, :859-888 show selected 0 with submit
age increasing 2 ->302. This is distinct from later steady selected-1 operation.
Simple medians of arbitrary fence increments or short destruction bursts are
biased by object batches and are not base-fps measurements.

## Cap, MFG parameters and limits

`fps_cap=58` is explicitly a **rendered** ceiling in common/settings.h:223-226;
`layer/streamline.cpp:999-1014` correctly supplies 17241 us to Reflex. The log
confirms it at :126 15:03:34.090. It is not a 58 displayed-fps limiter to divide
by three. :2767-2770 at 15:07:10.550-.572 removes it; subsequent focus pauses
continue, so the cap is not their necessary cause. Under ~25 rendered fps the
58 ceiling is not the observed throughput bound. Driver/display pacing can
still constrain output; neither these logs nor the INI establish actual scanout.

SDK sl_dlss_g.h:75-79 defines x3 as two generated frames, matching options
bg3rtx.log:824 at 15:03:46.457. Maximum generated count is five (:127), so x3
is supported. There is no evidence that the requested generated count is wrong.
Vulkan batch-present hook counts are a presentation proxy, not measured scanout;
the new SDK counter diagnostics must be read with their defined denominator.

The prior session used 2553x1048 NR slots (:2319,2339,2358), then ended amid
configuration change (:2736 15:03:00.697). It also records an SDK async-present
failure at :2699 15:02:59.480, code 0xC4653214, host frame 3009, subframe 2.
This is a real prior-session failure but has no demonstrated causal link to the
later session's focus pauses or NR skip path. The runtime staging log records
signed NVIDIA 310.8.0, matching source/destination hashes; it provides no NR
timing evidence. The current user INI has fps_cap 0, reflex 2 and dlss5_nr 1,
consistent with the final log settings rather than the whole session.

## Delivered changes and remaining check

Current `layer/streamline.cpp:2635-2640` queries SDK state once after each
present and accumulates its since-last-query count in the CPU-tested
`common/fg_present_stats.h`. The same sample supplies the input-consumption
fence. `layer/streamline.cpp:2579-2580` also requires successful tag/constants
publication before enabling FG; the earlier code could leave FG On after an
API publication failure. No such historical publication failure is proved here.
These are narrow correctness fixes, not a demonstrated resolution of the
reported stutter. No new key or speculative focus/VSync override is introduced.

`layer/streamline.cpp:2363-2403` measures required destroy waits and bounds
successful per-object logging; `:2677-2686` emits periodic SDK count ratios,
query failures, focus/pause notices, tag/constants status and wait total/max ms.
Wait targets and lifetime safeguards are retained. The ratio is an SDK reported
presentation count per rendered present, not a scanout measurement.

The one later explicitly authorized live check is described in
[the progress fragment](../progress/fgx3.md): hold focus through x2/x3/x2 in one
real Ultra + NR run, verify successful publication and stable SDK multipliers,
and measure destroy-wait total/max ms alongside any pauses. Actual display
cadence needs display-change timing. Until then, stutter improvement and GPU
cost remain unmeasured; offline tests do not substitute for that evidence.
