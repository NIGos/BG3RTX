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

The Round 2 clarification and enabled-only analysis below supersede any
interpretation that focus loss explains the in-game x3 symptom. An uninterrupted
x3 window has seven frame gaps over 100 ms at 21.48 rendered fps.

## Round 1 findings: interpolation interruptions

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

## Round 1 changes and remaining check (code pointers at 4ddec20f)

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

## Round 2: x3 in game

### A. Retained runs and enabled x3 evidence

The user's clarification changes the deciding question: they remained in game
except to change FG through the launcher. Focus pauses explain interruptions,
but cannot explain all poor x3 cadence while SDK interpolation stays enabled.
In particular, the enabled x3 interval 15:07:12.105–15:07:34.117 contains
450 sampled rendered frames in 20.947 s (**21.48 fps**), **seven >100 ms SDK
timer resets**, and 276 successful destroy waits with unknown duration.
The frame cap was already removed. These enabled-period gaps take priority over
focus as the still-unexplained symptom; GPU cost/p99/serialization are unmeasured.

All 220 retained main queue logs (519.9 MB) and all 1,024 results rows were
scanned read-only. No x3 or x4 run marker survives in that corpus; 220 raw logs
contain x2 markers and 193 results rows disclose a mode. Other result rows have
unknown mode. `test-main/captures/queue` is absent. Historical TRACK_B_FG x3
evidence is TV-standby functionality with dropped presents, not modern x3
cadence or quality qualification. Its live-display table measures x2/x4 only.

| Scene/evidence | x2 | x3 | Qualification limit |
|---|---|---|---|
| Water Queens House / S8 | 69 named logs; whole-run median fps 23.61–30.00 | no retained queue run | x3 unqualified |
| Camp | 26 named logs; 19.80–29.99 fps | no retained queue run | x3 unqualified |
| Inn / Mermaid | 41 named logs; 24.60–29.99 fps | no retained queue run | x3 unqualified |
| Dialogue | 38 named logs; 0–30.00 fps including startup | no retained queue run | x3 unqualified |
| Other / unassigned | 46 logs; 18.29–36.00 fps | no retained queue run | scene/mode controls insufficient |
| User's enabled moving intervals, unnamed scene, Ultra + NR | neighboring intervals 27.31–33.02 fps | mostly 21.48–30.93 fps; seven enabled timer resets | no controlled camera, GPU-ms, scanout ratio or frame p99 |
| Historical 2026-10-26 | live x2 proxy ratio 2 | standby x3 SDK count 3; dropped frames | functional only; no x3 smoothness pass |

Full denominators, every enabled interval, exact log lines, GPU/pace availability
and x2/x4 historical limits are in [the run appendix](fgx3-round2-runs.md).
[The 220-row inventory](fgx3-round2-runs.csv) preserves every retained raw run's
hash, result-row references and metric summaries. Scene buckets are inferred
from names; whole-run ranges include loading/toggles/captures and are not gates.
No actual x3 stutter fix is claimed by round 1 or by this evidence mining.

Even perfect multiplication of the sampled 21.48 rendered fps gives only
64.44 presented frames/s at x3, versus 66.04 at x2 with 33.02 rendered fps.
These are arithmetic examples from unequal moving windows, **not measured
display rates or an x3 effect cost**. They show why reduced native throughput
can erase the nominal multiplier gain even with interpolation continuously On.
The bad x3 native average is 46.55 ms, versus 34.77 ms in the first long enabled
x2 interval; the 11.78 ms difference cannot be assigned to a pass or wait without
matched scene/timing data.

### B. MFG pacing requirements

[The full line-by-line contract audit](fgx3-pacing-audit.md) records every public
checklist item, including unqualified items and requirements absent from this
header version. Audit code pointers are pinned to Round 1 commit `4ddec20f`.

| Requirement | Verified / gap | Consequence for this x3 report |
|---|---|---|
| MFG supported, generated count within max | x3 requests 2; reported max 5 (sl.log:488; bg3rtx.log:127,824). Generic code does not clamp to runtime max. | Correct count for this RTX 5090. |
| Swapchain and acquire/present GPU semaphores | Game asks 3 buffers, SDK uses 6 (sl.log:522,531); mode FIFO (bg3rtx.log:132,134). Proxies retain wait semaphores. | No documented missing x3 buffer requirement or proved shortage. |
| Options/constants/Present markers match frame | Same present thread/token; one constants publication. Constants are late, and simulation markers are synthetic. | Full Reflex timing qualification is missing, but no token mismatch is logged. |
| MFG metering | SDK available (:489), but baseline manual setup omits public extension/feature enablement (streamline.cpp device setup; log :41,75). Six batch-1 samples are unfocused startup. FC feedback 1 then good at :638,641. | Proven public setup omission, unknown private SDK fallback. Cold fg_present_metering=0 is a separate support-checked candidate; new counters expose batching. |
| VSync support | SDK explicitly excludes Vulkan; game FIFO remains requested and clamp warnings continue (:605-663). | Unsupported configuration is a plausible pacing factor, not a demonstrated cause of the seven gaps. |
| Input lifetime synchronization | Default SDK mode blocks the presenting queue until FG GPU work completes; host additionally waits for the global latest input fence on old resource destruction. | Longer FG work can reduce rendered throughput; avoidable host coupling is the candidate fix. No generation GPU cost is measured. |
| Correct reset and transition handling | reset always false; invalid frames disable FG but do not null all tags; no forced recreation on every On/Off. | Scene-cut/transition and Off-path gaps; not proven steady x3 causes. Changing x2 to x3 while On has no mandated reset/recreation. |
| Full input/Reflex/debug/quality qualification | x2 tests exist; x3 + Ultra + NR enhanced-debug and display-cadence tests do not. | Old functional MFG support is not a smoothness pass. |

The public Vulkan metering contract also requires extension/feature enablement,
which the Streamline requirements list did not supply. The support-checked cold
candidate fg_present_metering=0 enables that public path without replacing SDK
batching; both experimental candidates require experimental_options=1 in the
central settings model. SDK availability/feedback alone does not establish public setup or
causality. See the audit's linked Khronos sources. This is a separate pacing
hypothesis from exact retirement; both keys remain off in every preset.

Other checklist gaps (status/minimum-size gating, dynamic resolution, signature
verification, motion blur and shutdown limitations) are enumerated in the audit.
They are not attributed to the enabled x3 stutter without evidence. NR records
before the game's hudless/UI composite, in the same primary command buffer
(ngx.cpp:1479-1531); it does not insert a second CPU wait after hudless capture.
The seven long gaps precede Boost (enabled only at 15:08:44.037, :3816), so
Boost is not their necessary cause. No speculative VSync override is introduced.

### C. Destruction, stall estimate and candidate policy

**Proven:** the baseline applies the newest global FG input fence to historical
watched inputs, even when current roles are 0 (`4ddec20f:streamline.cpp:2363-2427`,
tracker.cpp:751,778). The public SDK consumption fence permits waiting on the
resource's own last tagged present instead (guide 15.1:895-903). The existing
tracker deferred path polls incomplete destruction and retains allocation/memory
ownership (tracker.cpp:1092-1129). Newer unrelated input consumption is not a
requirement for destroying an older input once its own use is complete.

At least 354/535 image waits match earlier game composite colour handles:
178 UI and 176 hudless. They are output-sized by composite discovery's explicit
extent classification (streamline.cpp:2135-2208); the relevant ready-input log
lines :820-823 identify 3840 x 1600 hudless/UI and 2560 x 1067 depth/MV.
The exact extent of each of the unmatched 181 images is not logged. Two original
views per retired image can contribute multiple calls without multiple frames.
Only three NR private slots are allocated in the session; these destructions
are not NR slot churn. A matching handle's last **discovery** precedes destroy
by median 0.832 s (range 0.084-132.548 s; p95 22.545 s). Discovery is not final
SDK use, so these ages cannot replace a last-use fence.

The game source/allocator is unavailable. These logs name inputs when they are
discovered in composite draws, not when images are allocated. They therefore
prove transient colour/view retirement, not creation of a new game target every
frame or the engine reason for each burst. The bad enabled x3 interval contains
54 discovery lines versus 276 view/image wait calls; cadence is not one new
NR allocation per frame. Repeated discovery can name the same handle before its
submit is published. The 32-entry historical watch list deliberately keeps old
images eligible for waits after the live inputs have rotated away.

**Stall estimate:** the bad enabled x3 window spans 22.012 s, with 276 wait calls
(12.54 calls/s); its inner native sample averages 46.55 ms/frame. A ~40 ms gap
between successful wait log lines is the sum of unknown game work, scheduling
and waits. Each wait's measured lower bound from the old data is **0 ms**, not
40 ms; the call's timeout is 1 s, and result 0 only means success. A wait on an
incomplete latest fence can, in principle, consume most of a native frame and
prevent the render thread overlapping CPU work with FG. Its actual share is
unmeasured. SDK input consumption is not display scanout completion, so it is
incorrect to add two generated display intervals to estimate this fence wait.
The final enabled x2 interval also has 384 calls in 71.289 s (5.39 calls/s);
different scene/load conditions prevent a causal frequency comparison.

**Implemented candidate:** `fg_destroy_wait_exact=0` (experimental, Advanced,
live; all presets Off). Record images/views before SDK tag publication; bind
the actual nonzero consumption fence after present. View aliases resolve to the
image's latest use. Pending/failed state publication cannot retire against an
older fence. Known old uses retain their own fence instead of adopting a newer
unrelated global value. Exact mode polls with timeout 0; incomplete objects use
the existing tracker pending-destroy route. Unknown use falls back conservatively;
actual destruction and device teardown discard associations. A successful
candidate-only idle drain at swapchain destruction closes the SDK semaphore
lifetime; failed drains never establish completion. This is a rare lifecycle
boundary, not a per-frame idle. The default path preserves its blocking policy.
CPU tests cover pending/failed publication, view aliases, different semaphores,
zero values, reuse and swapchain-boundary completion.

### D/E. Verification still required

Foreground changes now record only executable basename and window class, sampled
at most every 250 ms, plus whether the known game window owns foreground. No
window titles or full executable paths are logged. Short focus changes can fall
between polls; SDK focus notices are independent evidence. Cadence summaries
identify exact/global polls, blocking attempts, pending attempts, wait ms and
metering feedback; swapchain logging snapshots the game's requested buffer count
before the SDK mutates the creation structure.

One later authorized batch should cold-start with fg_present_metering=1 (verify
supported/enabled once), then compare x2/x3/x2 and x3 exact Off/On/Off in the same
Ultra + NR scene with matched motion, without opening the launcher during the
measured windows. Use the existing automated toggles and restore settings. With
the candidate On, the FG destroy path must report **blocking 0**, exact polls must
occur, pending destruction must drain without lifetime/API/device faults, and
native p99/max and display-change cadence must improve beyond repeated-Off
variation. SDK ratio should approach 3 with sufficient refresh headroom, with
valid tags/constants and no unexpected metering/focus pauses. This first check
can reject the metering setup candidate but cannot isolate its benefit versus a
cold Off run; promotion needs a matched Off/On cold comparison. Proxy/SDK count
alone is not the smoothness gate. Per-pass GPU ms and wait/query/present/sleep
CPU timings must distinguish generation cost from host stalls. This candidate
is READY_FOR_LIVE, not a demonstrated x3 stutter resolution.

Round 2 implementation pointers (final worktree; audit baseline above remains
4ddec20f): metering support/enablement `layer/streamline.cpp:1672-1751`, caller
restoration `:1841`; exact fence selection/poll/recheck `:2495-2572`, reservation
`:2722-2743`, post-present publication `:2874-2890`; prior SDK idle proof `:762`,
foreground `:1498`, and cadence/CPU timing `:2916-2936`. Policy tests are
`tests/fg_input_retirement_test.cpp`, `tests/fg_copy_lease_test.cpp`,
`tests/sl_present_metering_test.cpp`, and `tests/fg_present_stats_test.cpp`.
Expired semaphore epochs stay pinned across failed drains and new publications;
only a later proved idle of already-returned presents can release them. A prior
real idle proof survives normal resize, while new/unsubmitted uses cannot
inherit it. Private copy leases and tracked game colour images retain separate
ownership machinery; the new map is cleaned even after a game image rotates
out of the 32-entry historical watch list.

Final Round 2 offline validation: `scripts/build.ps1 -Test` passes **206/206**
CTests in 138.61 s, including QA detectors, settings/schema, retirement, copy
leases, metering, CPU timing and teardown fixtures. Settings also pass 4,352
checks directly. Defaults/schema were regenerated; `git diff --check` passes.
The test environment explicitly uses native Windows PowerShell modules for
Python-launched installer fixtures; inherited PowerShell 7 module paths caused
the earlier unrelated Get-FileHash failures. Final log:
`C:/Users/quali/Documents/Code Projects/BG3RTX/.scratch/coord/fgx3/round2-pass-build-test.log`.
No game was launched, no queue files were written, no runtime GPU cost or
smoothness improvement is claimed.
