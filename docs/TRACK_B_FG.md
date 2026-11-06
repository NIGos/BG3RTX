# Track B: DLSS Frame Generation through Streamline

## Release audit and load-time present cost - 2026-11-06 (fgx3, Round 3)

See [Round 3 audit](evidence/fgx3-analysis.md) and its four-run/release appendices.
Warmed S8 x3 reaches ratio 3 at roughly 30 rendered fps; metering is rejected,
not promoted. All four launches have a slow first **x2** enable before final
scene readiness: outer SDK present means 34-108 ms, inflated GPU intervals,
zero FG destruction waits, negligible GetState/ReflexSleep. Exact SDK/driver/WSI
backpressure remains unproved; this does not qualify uncapped foreground x3.

Quiet release no longer runs routine FG/lifetime timers, cadence formatting,
UI-handle log churn or pace-vector sorting. Release pins survive raw overrides
and the Detailed logging startup latch; support statistics remain explicit.
The 47-key hot cache refreshes on actual changes, not every present. Required
fences and the 0.026 ms median correctness copy remain. Phase resets clear all
statistics/baselines; detailed raw-present host/worker timing targets the open
stall. Mandatory SDK exports and adapter generation limits are checked.
No new key, ABI or shader variant; no dlss5.cpp/ngx.cpp edits. Full tests pass
206/206, plus lean diagnostic-off layer compilation. No new live is claimed.

## FG x3 retirement candidate - 2026-11-06 (fgx3, offline only)

`fg_destroy_wait_exact=0` preserves the global-latest-fence policy by default.
With 1, each actually tagged game image retains its own last present's
input-consumption fence; private depth/MV copies keep their existing slot leases. Tagged game views resolve to the same image lease.
Disabled optional tags create no new lease. Tracking remains active while the
candidate is off so a live policy switch can use historical tags safely.

A tag reserves the image before SDK publication. A pending or unpublished use
cannot retire against an earlier global fence. A later valid SDK state may
conservatively cover an earlier failed query; zero/missing semaphore values do
not establish completion. Host-off presents restore the prior lease because
they introduce no new FG consumption. Unknown uses fall back conservatively;
without a valid fence they remain deferred. Completed snapshots are rechecked
against a concurrent retag before destruction is permitted.

The candidate polls with timeout zero and delegates incomplete destruction to
the existing tracker pending-destroy queue, which retains callbacks and memory.
It introduces no per-frame device idle. At swapchain destruction only, it drains
SL's device proxy before the SDK may release its semaphore. Only successful
idle establishes completion; cached semaphore pairs expire at this boundary.
The game's successful proxy idle also records proof for already-returned
presents; future tags and unsubmitted copies do not inherit that proof. Normal
resize preserves a prior idle proof until another actual use invalidates it.
Without a successful drain, a new SDK semaphore cannot retire an expired-epoch
use. Such images and private copies stay pinned until a later successful drain
or device teardown, including when the experimental policy is switched off.
Private input-copy leases preserve written/layout state and keep unsubmitted
writes reserved. Device teardown clears all association maps.

SDK guide section 15.1 defines the fence as **input consumption**. It does not
say that both generated x3 frames have reached the screen. The old per-frame
log cadence and Vulkan success result do not measure time blocked. This is a
safe pacing hypothesis, not a measured stutter fix or a GPU cost claim.

The existing cadence line now distinguishes blocking waits, polls, exact/global
selection and unpublished deferrals, plus CPU mean/max times for proxy present,
GetState and ReflexSleep (all calls, including API failures). It also reports metered presents, largest
batch and separate numeric/good feedback notices. These are observations, not
extra display-frame counts. Foreground transitions sample at 250 ms and log
only executable basename/window class, never titles. Swapchain logging snapshots
the game's requested present mode/count before the SDK proxy can modify them.

CPU tests cover pending/zero/later publication, aliasing views, policy switching,
semaphore changes, idle failure/success and reuse. Experimental options exposes
"Frame generation pacing test" on Advanced; every preset leaves it off. A later
one-run x2/x3 A/B must show no blocking FG destruction waits with the key on,
stable SDK output count and improved cadence without resource/API faults.
No runtime qualification, ABI change or shader request delta (0).

### Optional Vulkan presentation capability

`fg_present_metering=0` is a separate cold candidate, "Multi-frame presentation
test" under experimental Advanced options. Key on queries physical support and
requests both `VK_NV_present_metering` and `presentMetering=VK_TRUE` only when
both are supported. The existing feature-chain helper is used; a caller-owned
feature field is restored before vkCreateDevice returns, including failure and
retry. Key off preserves any application-provided extension/feature choice.

This fills an omitted optional capability, not proof of the reported stutter's
cause. The SDK log saying metering is available does not prove that the host
enabled the Vulkan extension and feature. No SDK DLL is patched, and no
VkSetPresentConfigNV batches are synthesized by the layer. Startup logs report
requested, supported, queried and enabled state; subsequent cadence logs expose
actual observed metered presents, maximum batch and feedback notices.
CPU tests cover unavailable extension/feature, key off, enable and restoration
of caller-owned false/true feature fields. Both candidates stay off in every
preset; metering changes require restart. No live pacing or cost pass is claimed.

## Character-edge attribution prepared - 2026-11-03 (edge-flicker, offline only)

`tools/live_batches/edge_flicker` prepares one static Test Dialogue3 run: independent
FG, AO, sun, GI, RR, CBT and post-order toggles bracketed by baseline repeats.
K8 native and P16 displayed captures occupy separate windows. Diagnostic `pan_edge=0`
by default; no ABI/shader change. Budget 1.478 GB, scene 180 s, estimated 5 min with load.
The analyzer reuses QA temporal tests and dialogue shot registration. Half-size
samples, animation and capture-induced cadence limit attribution; parity is not
generated-frame identity. No live result, GPU capture cost or quality pass yet.

## Default promotion - 2026-10-30

`fg_input_copy=1` is now the default for input correctness. The previous sections
record historical off-default decisions, superseded here. One run with four fast-Q
windows per setting gives world 7.039 +/- 0.352 -> 7.940 +/- 0.100 and UI ROI
7.458 +/- 0.509 -> 7.835 +/- 0.447 codes (mean +/- window SE). The observed
worsening is below the simultaneous one-sided 95% Welch noise bounds, 1.075 /
1.663 codes, across the two measures. This is not an equivalence or full flicker
quality claim. Prior copy-0 SD 0.831 / 0.055; new copy-0 SD 0.704 / 1.017.

Input preservation already passed 200/200 private depth/MV matches with zero
invalid private samples; GPU copy median 0.026 ms. Steady FG ratio 2.00, zero
host toggles or new VUID IDs. PNG windows still reset timers (33 times), so the
proxy retains the documented cadence/endpoint limitations. User check: restart,
The Blushing Mermaid - 96h 39m, FG 2, rotate with overhead UI, compare copy 1/0.
Job `sup-fg-copy-default`, evidence `docs/evidence/fg-copy-default-20261030.json`.

## Submit-order / UI follow-up - 2026-10-30 (`066b38b`, defaults unchanged)

Color/UI used to be published while recording; private inputs and camera were
published at submit. Camera constants were then read outside the snapshot lock.
`fg_input_copy=1` now publishes composite resources at submit, snapshots camera
constants with them, and selects private inputs only for that same submit frame.
Secondaries forward the private pair. `fg_tag_ui=0` / `fg_tag_hudless=0` now send
explicit null tags, following the SDK cache contract. No ABI or shader changes.

The same-evaluate bit comparisons pass 200/200 for each private input, with zero
invalid samples. All 245 selected submit/token stamps align. This repairs hazards;
the original proxy regression is not causally attributed to a proven input lag.
Camera reprojection verifies MV scale 0.9994, median error 0.61 render pixels versus
104.65 for the opposite sign. Copy GPU median stays 0.026 ms.

Final fast 0/1/0/1 world proxy: 7.21 / 3.87 / 8.38 / 8.38 codes; UI ROI:
7.24 / 3.97 / 7.31 / 8.35. The second UI repeat fails the requested non-regression
check. Steady ratio 2.00; captured ratios 1.70-2.30 with 16 timer resets. PNG IO is
now off the present thread and bounded; synchronous GPU readback remains a limit.
Image-matched companion captures contain repeated natives and native frame gaps
(e.g. 15300 -> 15302 in UI burst 1097), so parity alone is not sufficient.

The UI matrix tests (UI,PQ,hudless) = (1,0,1), (0,0,1), (1,1,1), (1,0,0),
(1,1,0), twice, in one FG 2 selected-character rotation run. Both baseline near-UI
foliage reprojection supports are zero. Far class 4 means are 7.80, 8.52, 8.08,
8.06, 8.32 respectively. They have different camera/material support and cannot
rank causal hover-excess removal. No setting qualifies for a new default.

RR guide-pool rollover also exposed an idle/reset race with the FG pacer; see
TRACK_A_RR. Repaired runs survive 64 rollovers. No new VUID IDs in the final fast
job; one SDK latency-marker threading warning remains. All quality defaults stay
unchanged and the branch is not merged. Experimental virtual mouse controls were
removed. Evidence: `docs/evidence/fg-followup-20261030.json`. Build 14/14, shaders
107/107, QA detectors pass; all 44 new completed raw bursts analysed and deleted.
Check The Blushing Mermaid - 96h 39m with FG 2, real rotation and a visible NPC
nameplate/selection UI over foliage. Next qualify cadence and actual near-UI support.

## Copy motion follow-up - 2026-10-30 (`sup/fg-copy-gate`, default 0)

The inn Q/E probe proved that posted key repeat is not a speed control. The follow-up
uses tested duty cycles for slow keys, held keys for fast rotation, and a pan, copy
0/1/0/1 for each. Native input bursts and presented PNGs occupy separate windows.
Slow input still aliases with stalls; the pan drifts away from the inn; UI support
varies. This is a failed quality qualification, not evidence for promotion.

Validity: copy off has 42/96 entirely zero late depth samples and 4,890,263 non-finite
late MV components. Copy on has 101/101 private depth/MV pairs equal to NGX, zero
all-zero private depth samples and zero non-finite private MV components. Private
validity is captured at copy time; selected private resources are guarded until SDK
consumption. Original late images remain corrupt with the key on.

Fast presented midpoint residual off -> on (code values): world 1.90 -> 8.93,
foliage ROI 2.44 -> 9.93, character ROI 2.45 -> 9.16, world-UI ROI 2.07 -> 9.44.
These unwarped fixed-ROI proxies are review only. Probe odd PNGs match rendered
endpoints exactly; main separated captures infer parity and lack simultaneous
endpoint identities. A lower proxy would not establish a quality improvement either.
`fg_motion_gate.phase_metrics` and `fg_static_effects.same_pose` are reused; those
tools' original gates do not score generated-frame interpolation error.

No host FG toggles during the matrix. Steady ratio 2.00, but 33/462 sampled lines
range away from it (full range 1.60-2.10); 85 SDK frame-timer resets. No new VUIDs,
NVIDIA events or dumps. GPU copy median 0.026 ms. Default stays 0; no merge.
Raw burst directories are deleted after analysis. Evidence and paths:
`docs/evidence/fg-copy-gate-20261030.json`. Next: capture at normal cadence with
explicit rendered/generated endpoint identity and matched in-world UI during motion.

The rotation-only foliage follow-up measures native-final class 4 reprojected change
(codes/255): FG 0 clear 6.68 / 9.99, hover-request 7.17 / 6.47; FG 2 clear
10.05 / 8.88, hover-request 11.07 / 10.82. FG 0 watch ratio 1.00; FG 2 SDK
median 2.00, no interior host toggles. Hover world UI pixels are 2,547 / 15,008
at FG 0 and 14,905 / 0 at FG 2; clear windows have 0. Ring/nameplate matching
is unproved and one hover has no world UI. These pre-FG residuals include physical
animation and reprojection error. No RR-versus-FG attribution; no foliage fix.
Build -Test passes all 13 CTests; camera schedule/permission checks 25/25.

## Transient FG inputs - 2026-10-30 (`sup/ui-inputs`, default off)

The inn's captured floating text is in the UI image, absent from RR input colour and
hudless. Other in-world UI types are not all qualified. Raw sRGB UI does not satisfy
HDR10 composition: mean error 0.085 on UI pixels, zero outside. Existing fg_ui_pq=1
reduces it to 0.000075 but remains off without a qualified visual gate.

The deciding pair gate finds late depth entirely cleared to zero in 64/128 samples
(8/16 in each burst), after valid nonzero depth at NGX evaluate. The MV image is finite
before/after RR and its post replay, then has 3 non-finite components in render pixels
x 512..551, y 80..103 at UI composite. Captures locate the intervening damage, not its
writer. Input layouts are also stale: depth tag 3 and MV tag 2 versus observed layout 5.
The game performs these transitions in CmdWaitEvents(2), which FG tracking omitted.

`fg_input_copy=1` preserves the actual depth/MV pair before evaluate in private D24S8
and RG16F images. Depth has a depth-only view. Submit order selects the matching pair;
present's counter is submit's counter plus 1. The option also tracks event-wait layouts
for FG tags. Diagnostics maintain independent observations, leaving key-off tags intact.

A slot cannot be reused until the SDK publishes a nonzero input-consumption fence value
and it completes. Earlier submitted copies deselected by a toggle retire under the next
present's fence; unsubmitted/future copies remain reserved. CPU tests cover these cases.
Eight busy slots fall back safely and log failure. Resizing and device teardown release
both images only after consumption. There is no ABI or shader change.

`sup-ui-inputs-pair-gate`: 8 valid bursts, 17 lighting and at least 16 input frames each;
68/68 MV and 68/68 masked D24 copies match NGX. Median GPU pair-copy cost 0.026 ms.
Steady-state FG 2.00, status/API errors 0; no new VUIDs during measurement, no VK_ERROR,
NVIDIA events or dumps. Six existing startup validation IDs precede the measurement.
The earlier lease stress completed 16 on/off cycles without exhaustion.

The full quality gate is NOT passed. Static native-final class 4 tflk off 1.51 / 1.45%,
on 1.43 / 1.63%; energy +0.020%, within off-repeat spread 0.126%. Native final precedes
UI and FG. Slow posted rotation is intermittent; views and near-UI coverage differ.
Presented PNG readbacks stall, so their metrics are review-only. Keep default 0.

After restarting, load The Blushing Mermaid - 96h 39m and compare fg_input_copy=0/1
with floating text/selection UI visible while rotating continuously. Keep fg_ui_pq=0
for that comparison. Evidence and limitations: docs/evidence/ui-inputs-20261030.json.

## Live FG matrix — 2026-10-29, no promotion

Static same-view parity PASS: 30 debug traces, FG 2 primary 99.5195-99.5215%,
18.7-18.8 rendered fps, displayed/rendered 2.00; FG 0 99.516%, 18.2 fps, 1.00.
All effects have dispatch/timing evidence; GI debug is black in both modes, so
nonzero GI energy is not proved. Sheets: captures/p1-fg/static-effects-sheets/.
Visible non-activating native-windowed real presentation worked without observed
activation; window coverage was 100%. Latest user authorization waives USER_AWAY
only for explicit visible_unfocused_fg. Default visible_unfocused remains FG 0.

Motion qualification INVALID: stopped after 13/56 complete phases plus partial
phase 13. Posted 60 px/s rotation has stationary periods and up to 98.58 deg/frame
jumps. Baseline FG 2 median ratio 1.00; rebase 1.163, guard 1.412. Corresponding
primary medians 21.299 / 85.984 / 20.5465%, required 99.501%. RT remained active.
No quality success or renderer attribution from these confounded paths/views.
6,106 retired-only destructions reset stability with guard off; 3,279 did not
with it on. First deciding interior mode changes 0 baseline vs 0 guard, so the
required reduction is unproved. Key stays default-off; no merge. No SDK focus
disable/crash exception; invalid-handle teardown followed deliberate game close.
Next qualify relative background rotation, then repeat sustained FG-on A/B.
Source logs p1-fg-{static-effects.1.152122,motion-matrix.1.153616}; details and
phase numbers in docs/evidence/p1-fg-live-20261029.json and PROGRESS_CODEX.md.


## P1 FG-on deciding matrix — 2026-10-29, queued preparation

The user's current symptom occurs with FG on. FG 2 is now the deciding P1
configuration; FG 0 is reference only. Both run focused/real in one moving
session, with Streamline cold-loaded at FG 2. Unfocused launch remains FG-off
only and is not a deciding test. No game launched or runtime pass established.

fg_current_destroy_only is compared against baseline and rebase-only, separately.
Retain it only if actual FG generation stays active, moving primary match
reaches the FG-on static level, and interior host toggles decrease. Report both
all options calls and mode changes; planned switches at phase boundaries do
not count as interior instability. SDK state and actual displayed/rendered
are required: requesting FG on is insufficient. A static same-view job first
checks sun/local/GI/reflection/RR parity through debug captures and counters.
Both keys stay default-off until deciding evidence exists. Reflex/TLAS timing
causation is unproved; no Reflex path change was made. Details and expected
report paths are at the top of PROGRESS_CODEX.md.

## P2 watched-image retirement candidate — 2026-10-29, unmeasured

Log-only review: retained user excerpt 09:29-09:34 has 27 host on/off changes,
peak 16 per 60 seconds. All 13 host offs follow image destruction by 2-63 ms.
The SDK also explicitly disables interpolation three times because the window
is not focused while requested mode remains on. Focus loss is not invalid input.
Legacy logs do not name the destroyed image/role; exact input attribution is
unavailable. A changed hudless/UI handle alone does not establish invalidity.

Queue readiness diagnostics provide stronger attribution: of 20 host offs,
17 have current inputs ready, gate 0x80 (destruction cooldown). Three have
gate 0x40, UI age 4 during swapchain recreation. These stale UI shutdowns remain
necessary. Current-ready state does not establish the retired/current role of
the image destroyed in the preceding frame. Full per-off source lines are in
docs/evidence/p2-fg-and-rt-log-review.json.

`fg_current_destroy_only=0` is a new live, default-off candidate. Legacy behavior
resets stability for any image in the 32-entry historical layout watch list.
With the key on, only destruction of a currently selected depth/MV/hudless/UI
image resets stability. Rotated-out images still wait for the DLSS-G inputs
consumption fence before their views/images are destroyed, and layout/watch
cleanup still occurs. Missing/stale current inputs and real target recreation
keep the existing off/cooldown behavior. No change to the legacy cooldown when
the key is off. No shader or ABI change; CPU/GPU cost is unmeasured.

Every host options decision logs reason, missing/stale role masks (depth=1,
MV=2, hudless=4, UI=8), handles/views, extent, age, layout, tag/constants API
results, and last destruction/current roles plus cooldown reset frame. These
logs distinguish actual current-input loss from a historical-image reset and
flags-only options calls. SDK focus messages distinguish independent gating.

Seven CTests pass, including CPU cooldown/retirement scenarios; four audit
tests reject conflating flags or SDK focus with input failure. Evidence:
docs/evidence/p2-fg-and-rt-log-review.json. No game launched and no runtime pass.
Candidate remains off. The two P1 rotation jobs remain unchanged and queued.
Next P2 gate: within one real-present FG run, observe key off/on and current
role per toggle, keep genuine invalid-input shutdowns/fence safety, and reduce
retired-only cooldowns without API/fence faults. No promotion before evidence.

`layer/streamline.cpp` runs NVIDIA DLSS-G in BG3's Vulkan renderer, including
multi frame generation on RTX 50. It uses Streamline 2.14.16 with manual
hooking from inside the host layer. NVIDIA Smooth Motion is not involved:
`launch-bg3.ps1 -Clean` disables `VK_LAYER_NV_present`.

**Status (2026-10-26).** DLSS-G works in game at 2x, 3x and 4x, together with
the game's own DLSS SR, and survives the loading-screen to gameplay
transition. On a live 120 Hz display, FG off presents 120 fps. FG 2x
renders 60 fps and **displays 120 (ratio 2.00)**. FG 4x renders 40 fps and
**displays 120 (ratio 3.00)**. Streamline reports
`numFramesActuallyPresented` = 2 and 4, status 0, and no API errors. In both
cases DLSS-G paces its output to the display refresh. Runs lasted 8 to 11.5
minutes, with more than 7 minutes of frame generation in the last one.

## Setup

1. Download `streamline-sdk-v2.14.16.zip` from
   github.com/NVIDIA-RTX/Streamline/releases. Unpack `bin/x64` (including
   `bin/x64/development`) and `include` into
   `external/streamline-sdk-v2.14.16`. CMake looks there first, then in
   `$env:BG3RTX_SL_SDK`, then in the main checkout's `external/` when it builds
   from a worktree.
2. `scripts\build.ps1` copies NVIDIA's signed binaries next to the layer:
   - `bin\sl\` holds the production DLLs (the default);
   - `bin\sl_dev\` holds the development DLLs. They log more and support hotkeys, but they burn a "DLSSG SDK - DO NOT DISTRIBUTE" watermark into the image.

   The copied files are `sl.interposer`, `sl.common`, `sl.dlss_g`, `sl.reflex`,
   `sl.pcl`, `nvngx_dlssg`, `NvLowLatencyVk`, and `nvngx_dlss`/`nvngx_dlssd`. The
   last two are for the game's DLSS SR, which shares Streamline's NGX instance
   (see below). Nothing goes into the game folder.
3. Set `fg=2` (or 3 or 4) in `bg3rtx.ini` and launch as usual. To run unattended:
   `scripts\auto-run.ps1 -Set fg=2,auto_trace=0 -TimeoutSec 600`.

| key | default | meaning |
|---|---|---|
| `fg` | 0 | 0 = off (Streamline is not loaded at all), 2 / 3 / 4 = 2x / 3x / 4x. 0 to 2+ needs a restart; 2, 3, 4 and 0 switch live once Streamline is loaded |
| `sl_dev` | 0 | 1 loads the development DLLs from `bin\sl_dev` |
| `sl_verbose` | 0 | Streamline verbose logging, into `bg3rtx.log` and `sl_logs\sl.log` |
| `fg_game_ngx` | 2 | what the game's own NGX init does. 2 = it shares Streamline's NGX instance, 1 = blocked (the game falls back to TAA), 0 = untouched (broken, see below) |
| `reflex` | 1 | 0 = off, 1 = low latency, 2 = low latency + boost. Reflex/PCL markers are always sent, because DLSS-G needs them |
| `fg_stable_frames` | 30 | frames with valid inputs, and no tagged image destroyed, before DLSS-G switches on |
| `fg_native_of` | 1 | 1 = native optical-flow queue, 0 = interop mode |
| `fg_only_interpolated` | 0 | present only generated frames (`eShowOnlyInterpolatedFrame`) |
| `fg_menu_detect` | 1 | DLSS-G's full-screen menu detection (uses the UI tag) |
| `fg_ui_recompose` | 0 | user interface recomposition (see [Known issues](#known-issues)) |
| `fg_tag_hudless` / `fg_tag_ui` / `fg_tag_backbuffer` | 1 | optional tags |
| `fg_mvec_scale_x` / `_y` | -1 / -1 | multiplied by 1/render size to give `mvecScale` |
| `fg_camera` / `fg_jitter` | 1 / 1 | 0 = identity matrices / zero jitter |
| `fg_snap` / `fg_snap_on` / `fg_snap_after` | 0 / 1 / 60 | capture the next N images actually presented, as PNGs in `captures\fg_<stamp>\`. The capture starts after the `fg_snap_on`-th time FG switches on, plus `fg_snap_after` frames |
| `fg_state_every` | 60 | log `DLSSGState` every N frames while FG runs |
| `sl_log_barriers` / `fg_log_presents` / `fg_log_camera` | 0 / 6 / 0 | debug logging |
| `sl_init_at` | auto | 0 = `slSetVulkanInfo` at the first `vkGetDeviceQueue`, 1 = at the first swapchain. The default is 0 when the NGX instance is shared |
| `sl_init_after_create` | 0 | with `sl_init_at=0`: run `slSetVulkanInfo` at the game's NGX Init (stubbed while shared) instead of the first `vkGetDeviceQueue`. ReShade (a layer above ours) gets queues inside its own `vkCreateDevice`, before the loader has filled the device table, so NGX loaded null device functions and Streamline crashed (2026-11-05, `common/sl_init_trigger.h`). The first swapchain stays the fallback |
| `sl_veh` | 1 | log fatal exceptions (module+offset and stack) before the game's crash handler exits silently |

## Verified with the display on (2026-10-26)
All runs used real presentation and HDR10 output (swapchain RGB10A2), with RR, RT shadows, the alpha-tested BVH and ray-traced GI on.

| mode | rendered fps | displayed fps | ratio | `numFramesActuallyPresented` |
|---|---|---|---|---|
| `fg=2` | about 103 | about 206 | 2.00 | 2 |
| `fg=4` | about 66 | about 266 | 4.00 | 4 |

- **Motion.** `auto-run -Move` pans the camera out over the docks. Four consecutive presented frames, rendered and generated alternating, are in `img/fg_pan_consecutive_frames.png`. The pier moves smoothly with no doubling or trails, which confirms the motion-vector sign and the camera matrices.
- **UI.** The hotbar stays sharp and steady in every presented frame during the pan. There is no HUD ghosting.
- **Stability.** 4-minute runs at 2x and 4x: status 0x0, no API errors.
- `fg=2` is now the default in `bg3rtx.ini` for play. Headless test runs force `fg=0`.

## How it is wired

```
bg3.exe -> loader -> VK_LAYER_BG3RTX_host -- swapchain/acquire/present/DeviceWaitIdle --> sl.interposer proxies
                          |                                                                    |
                          +------------------------- everything else ----------------------+   v
                                                                                  next layer -> driver
```

### Instance (`vkCreateInstance`)

- **slInit.** The layer loads `sl.interposer.dll` and calls `slInit`:
  - flags `eUseManualHooking`, `eDisableCLStateTracking`, `eDisableDebugText` and `eUseFrameBasedResourceTagging`;
  - `renderAPI = eVulkan`;
  - features DLSS_G, Reflex and PCL.

  `slSetTagForFrame` returns `eErrorInvalidIntegration` without the last flag.
- **Instance requirements.** `slGetFeatureRequirements` is read for each feature. The layer adds `VK_KHR_external_semaphore_capabilities` and `VK_KHR_external_memory_capabilities`, and the API version goes from 1.1 to 1.3, as the interposer's own `vkCreateInstance` does.

### Device (`vkCreateDevice`)

- **Extensions added:**
  - `VK_NV_optical_flow`, `VK_KHR_external_memory(_win32)`, `VK_KHR_external_semaphore(_win32)`;
  - `VK_KHR_maintenance4`, `VK_NV_low_latency(2)`.

  `VK_KHR_buffer_device_address` is not added: the game already enables the EXT version, and both may not be enabled together (VUID-03328).
- **Features.** The layer turns on timeline semaphores, synchronization2, buffer device address and NV optical flow. Where the game's `pNext` chain already has the matching struct, the flag is set in it. Otherwise a struct is appended. The game chains only individual feature structs, never `Vulkan12Features`.
- **Queues.** The game creates family0 x1, family2 x1 and family1 x2. Streamline gets one more graphics queue (family 0, index 1) and two more compute queues (family 2, index 1). A dedicated optical-flow queue is created at family 5 (flags 0x10c), index 0, so optical flow runs natively.

### Streamline sits below this layer

Three problems came up, each found by running it in game:

1. **Present recursion.** `sl.interposer` builds the Vulkan table every plugin uses with `GetProcAddress(vulkan-1, "vkGetDeviceProcAddr")`. That leads back to the top of the chain, so every present Streamline made would recurse into this layer. The fix:
   - The layer patches the interposer's `GetProcAddress` import so that `vkGetDeviceProcAddr` resolves to `sl_next_gdpa`, which hands out the next layer's functions.
   - Command buffers and queues created that way skip the loader's trampolines. Their dispatch pointer is set with the loader's `SetDeviceLoaderData`.
   - Streamline's own presents go through a wrapper that counts them (`[watch] displayed`), can capture them (`fg_snap`) and logs them.
2. **Physical-device handles.** Instance-level functions stay on the loader's trampolines. `sl.common` initialises NGX with null proc-addr callbacks, so NGX loads vulkan-1 itself and needs the application-level `VkPhysicalDevice`. The loader wraps physical devices above the top layer, so the layer finds that handle with the loader's `vkEnumeratePhysicalDevices` and hands it to `slSetVulkanInfo`.
3. **When `slSetVulkanInfo` runs.** It must happen after `vkCreateDevice` has returned to the game. The loader fills a new device's trampoline table only after the layer chain returns. Called any earlier, NGX's DLSS-G snippet resolves every device function to null, then throws. The layer therefore calls it at the game's first `vkGetDeviceQueue`.

Other wiring:

- **Window lookup.** DLSS-G finds the window through the interposer's surface-to-HWND map, which only its `vkCreateWin32SurfaceKHR` proxy fills. Without that mapping swapchain creation fails with "Could not find a window". The layer remembers each surface's HWND. Just before the swapchain is created it calls the proxy once for that window, and answers the nested `vkCreateWin32SurfaceKHR` with the game's existing surface.
- **Re-entrancy.** `vkDeviceWaitIdle` calls that come back down while a Streamline proxy is running on that thread go straight to the next layer. The layer never holds its own lock while calling into Streamline, because NGX's submits and image destroys come back through the layer.

### The game's own NGX: the hardest problem

BG3 initialises NGX itself for DLSS SR (`NVSDK_NGX_VULKAN_Init_Ext`, same VkDevice). Whichever init runs last owns the process-wide NGX state:

| order | result |
|---|---|
| Streamline first, game second | DLSS-G `CreateFeature` fails with `0xbad0000b` (UnableToInitializeFeature) every frame. Optical-flow mode (native or interop) and dev or prod binaries make no difference |
| game first, Streamline second | NGX reports `numFramesToGenerateMax = 1` instead of 5. The first DLSS-G evaluation crashes inside the driver (`nvoglv64+0xfaad95`, in a barrier NGX records into Streamline's command buffer) |
| game's init blocked (`fg_game_ngx=1`) | DLSS-G works, but the game has no DLSS SR and renders natively |
| **shared (`fg_game_ngx=2`, default)** | **both work.** The layer patches bg3.exe's `GetProcAddress` import in memory only: the game's `NVSDK_NGX_VULKAN_Init*` returns success without re-initialising, its `NVSDK_NGX_VULKAN_Shutdown*` is a no-op, and everything else (CreateFeature, Evaluate) runs on Streamline's NGX instance. NGX looks for snippets in Streamline's plugin folder, so `nvngx_dlss.dll` and `nvngx_dlssd.dll` are copied there. The game's DLSS SR still runs: the motion-vector pass stays at 2560x1067 |

The same import patch logs every NGX entry point the game resolves: Init, Init_Ext, Init_ProjectID, Shutdown(1), Get/Allocate/Destroy/CapabilityParameters, GetScratchBufferSize, CreateFeature(1), EvaluateFeature and ReleaseFeature. **Track A (RR) should hook these same entry points** rather than add a third NGX instance.

### Per frame (`vkQueuePresentKHR`)

The frame token is created right after the previous present. It is followed by `slReflexSleep` and the PCL markers sim start, sim end and render start. At present the layer does the following, in order:

1. Sets the tags and `sl::Constants`.
2. Calls `slDLSSGSetOptions`, but only when something changes.
3. Marks render end and present start.
4. Calls Streamline's `vkQueuePresentKHR` proxy.
5. Marks present end.
6. Calls `slDLSSGGetState` to get the inputs-processing fence.

DLSS-G is only on while all of these hold:

- all four inputs were identified within the last 3 frames;
- they have been stable for `fg_stable_frames` frames;
- no image that has ever been a tag was destroyed in that window.

BG3 recreates its targets when the loading screen gives way to gameplay. With frame generation left on through that switch, the game stopped submitting for good (a hang). Switching off at the first destroyed tag image fixed it. If a tag that is in use is destroyed while FG runs, the layer also waits on DLSS-G's `inputsProcessingCompletionFence` before letting the destroy through.

## What is tagged

The image ids change between runs, so every input is found by its pass signature. All tags use `eValidUntilPresent` and no command buffer.

| tag | source | format / size |
|---|---|---|
| depth | depth attachment of the motion-vector pass (first fs `788dfdf60d8ed4cd`). The layer creates its own depth-only view | D24S8, render res |
| motion vectors | colour attachment 0 of that pass | RG16F, render res (the game keeps three in rotation) |
| hudless | the pass that draws into a swapchain image (the UI composite): the non-RGBA8 output-sized image in its set 1. With HDR10 output that is fs `046c6949`, binding 1. SDR output uses another shader (`d16f63d8`), so images are classified by size and format, not by shader | RGB10A2, 3840x1600 |
| UI colour + alpha | the same pass: the RGBA8 output-sized image in set 1 | RGBA8, 3840x1600 |
| backbuffer | extent only (Streamline owns the swapchain) | |

**Layouts.** Each tag carries the layout its image is in at present, in queue order:

- render-pass final layouts and barriers on watched images are recorded per command buffer;
- secondaries are folded into their primary;
- the recorded layouts are applied at `vkQueueSubmit`.

The layouts seen are depth 3 (`DEPTH_STENCIL_ATTACHMENT`), MV and hudless 2 (`COLOR_ATTACHMENT`), and UI 5 (`SHADER_READ_ONLY`).

**Motion vectors.** FRAME_MAP had this wrong; it is corrected now. G-buffer RT0 holds stereographic normals, and the pass that was called "velocity" (`f90209c7`) only copies them. The real motion vectors come from a later pass. Its first draw is a full-screen camera pass that reconstructs the world position from linear depth, projects it with the previous view-projection, and writes `(fragCoord + jitter) - (prevPixel + prevJitter)`. Draws for dynamic objects follow. The values are in pixels, current minus previous, with jitter removed. DLSS wants previous minus current, so:

- `mvecScale = (-1/2560, -1/1067)`;
- `motionVectorsJittered = false`;
- `depthInverted = true` (reversed Z, D24 values 0.001 to 0.03);
- `cameraMotionIncluded = true`.

The 2026-10-30 camera reprojection check verifies the sign and pixel scale; see the submit-order follow-up above.

### Camera constants

The motion-vector pass reads two uniform buffers at its first draw. Their contents are read on the CPU from the game's mapped memory: the layer tracks `vkMapMemory`, `vkBindBufferMemory(2)` and the dynamic offsets of the call that bound the set. The values are promoted at `vkQueueSubmit`. Logged values:

- **set 0 binding 2:**
  - `m0` = world view;
  - `m1` = jittered reversed-Z infinite projection (`P00` 0.936, `P11` 2.246, near 0.1, jitter in `P[2][0..1]`);
  - `m2` = camera-relative jittered view-projection;
  - floats `0.1, 40000` = near and far;
  - `m6` = camera position.
- **set 0 binding 5:**
  - `m2` = previous camera-relative view-projection;
  - `m3` = previous camera position;
  - `m5` / `m6` = current / previous jitter in pixels (for example -0.25, 0.1667).

The constants are built from them:

- **Unjittering.** Jitter is removed by post-multiplying with `S` (`S[3][0..1] = -P[2][0..1]`).
- **Clip-to-clip.** `clipToPrevClip = inv(VP*S) * T(camPos - prevCamPos) * VP_prev*S_prev`, and `prevClipToClip` is its inverse. With a static camera this is the identity to 1e-8.
- **View and camera.** `cameraViewToClip` is the unjittered `m1`. FOV comes out at 0.838 rad. Near and far come from the UBO. The camera basis is taken from `inv(VP*inv(P))`.
- **Jitter.** `jitterOffset = (P20*W/2, -P21*H/2)` pixels.

## Measurements

In each 5 s window:

- **rendered** counts the game's `vkQueuePresentKHR` calls (`[watch] presents`);
- **displayed** counts the presents Streamline issues to the swapchain, i.e. what passes through `w_QueuePresentKHR` (`[watch] displayed`).

### Live display (final run, 04:00 to 04:08, 120 Hz, SDR, DLSS SR Quality 2560x1067 to 3840x1600)

The TV woke up during this run. These are the representative numbers.

| mode | rendered / 5 s | displayed / 5 s | displayed fps | ratio | `numFramesActuallyPresented` |
|---|---|---|---|---|---|
| FG off (`fg=0` live, Streamline loaded) | 597 to 602 | 598 to 602 | 120 | 1.00 | 1 |
| fg=2 (2x) | 299 to 301 | 598 to 601 | 120 | **2.00** | 2 |
| fg=4 (4x MFG) | 199 to 200 | 597 to 600 | 120 | **3.00** | 4 |

- The game is limited to 120 fps without FG, because it is vsync-locked to the 120 Hz display.
- With FG on, DLSS-G paces output to the refresh rate, so the game renders at 120/N fps and the display still gets 120 fps.
- At 4x the displayed rate is capped by the refresh, so the measured ratio is 3.00 and not 4. DLSS-G still generates 3 frames per rendered frame (`numFramesActuallyPresented` 4).
- The full gain, more than 120 displayed fps or 120 rendered fps with 240+ displayed, needs a higher-refresh display or VRR with the game uncapped. That is not measured yet.

The run went 11.5 minutes in total with no crash or hang:

- loading screen;
- target recreation, with FG automatically off for 30 frames;
- gameplay at 2x for 3 minutes, then 4x for 4 minutes, then off.

### Display in standby (earlier runs, 00:00 to 03:56)

While the TV was in standby, Windows throttled presentation to about 2 Hz. The game showed 10 presents per 5 s with `fg=0`, whether Streamline was loaded or not.

| mode | rendered / 5 s | displayed / 5 s | `numFramesActuallyPresented` |
|---|---|---|---|
| fg=0 baseline | 10 | 10 | - |
| fg=2 (2x) | 12 to 16 | 8 to 12 | 2 |
| fg=3 (3x) | 11 to 13 | 9 to 11 | 3 |
| fg=4 (4x MFG) | 11 to 13 | 9 to 11 | 4 |

The throttled presentation engine dropped the generated frames: DLSS-G logged "CPU pacer is skipping the frame because of timeout". These rates mean nothing in absolute terms. They are kept here only as the record of the stability runs: 8, 10 and 9 minute runs with `fg_game_ngx=2`, including more than 4 minutes of FG in gameplay.

### Captured frames

`fg_snap` copies what Streamline actually presents, real and generated frames. The three captures are:

- `captures\fg_20261026-031933\`, with the development DLLs and their watermark;
- `captures\fg_20261026-032927\`;
- `captures\fg_20261026-040034\`.

All three caught loading screens: the "100%" screen before gameplay, and one shown during the area transition. Logo, text and spinner show no tearing or ghosting. No gameplay frame has been captured yet. `fg_snap_on=3` (the third enable), or a longer `fg_snap_after`, should land in gameplay.

### After merging main (Track A RR, GI, headless mode)

The merged build ran for 7 minutes with no crash (`fg=2`, main's defaults, display in standby again):

- **Track A on the shared instance.** RR=1 and Track A's `_nvngx.dll` inline hooks work on Streamline's NGX instance. The game's SR feature is created there ("CreateFeature feature 1 -> Success"), and RR's guides are built.
- **DLSS-G.** DLSS-G ran at status 0 with 2 frames presented per app frame.
- **Headless mode.** Main's `auto-run.ps1` now defaults to `headless=1` (fake acquire/present), which crashes the game a few seconds in when Streamline owns the swapchain. The layer now ignores `headless` when `fg>=2`. Pass `-RealPresent` anyway, to make the intent explicit.

## Script Extender (bg3se v33)

**Symptom.** With the Script Extender's `DWrite.dll` in `bin`, frame generation never shows generated frames. The run of 2026-10-26 14:46 logged `status 0x0, presented per app frame 2` and `displayed/rendered 1.00` for 35 s after `slDLSSGSetOptions(mode on)`, at 108 rendered fps. Without the Script Extender, the same line goes from 1.00 to 2.00 within 3-5 s of every enable (runs of 11:42 and 09:35).

**Mechanism (bg3se source, `main`, matches the v33 binaries in `%LOCALAPPDATA%\BG3ScriptExtender`).**
- The extender always builds a `VulkanBackend` when `bg3_dx11.exe` is not loaded (`Client/IMGUI/IMGUI.cpp`, `IMGUIManager` constructor). `ScriptExtender::Initialize` calls `imgui_.EnableHooks()` unconditionally (`Extender/ScriptExtender.cpp`). No key in `ScriptExtenderSettings.json` turns it off (`Shared/ExtenderConfig.inl`).
- `EnableHooks` Detours-patches the loader's `vkCreateInstance`. Its post-hook patches the loader's `vkCreateDevice` and `vkDestroyDevice`. The `vkCreateDevice` post-hook (`Vulkan.inl`, `vkCreateDeviceHooked`) resolves `vkCreatePipelineCache`, `vkCreateSwapchainKHR`, `vkDestroySwapchainKHR` and `vkQueuePresentKHR` with the loader's `vkGetDeviceProcAddr`. It then Detours-patches each one, after `ResolveFunctionTrampoline` (`CoreLib/Wrappers.h`) has followed any `E9` thunk to the function body. The loader answers device-level names from the device dispatch table, which this layer filled (`vkGetDeviceProcAddr` in `layer.cpp`, the `hooks()` map). So the code that gets patched is **this layer's** `QueuePresentKHR` (`layer.cpp`) and `CreateSwapchainKHR`/`DestroySwapchainKHR` (`tracker.cpp`). `vkCreatePipelineCache` is not ours; that patch lands below us.
- It is an inline patch, not a pointer swap: every present the game makes, through the loader trampoline or a cached pointer, runs the extender's pre-hook first, then our `QueuePresentKHR` → `sl_present` → Streamline's proxy.
- What the hook does depends on which present it is:
  - **On the first present of each swapchain**, `vkQueuePresentKHRHooked` sees the handle that its `vkCreateSwapchainKHR` post-hook stored. That is the handle Streamline returned, so it matches.
  - **The `vkCreateSwapchainKHR` post-hook** (`collectSwapChainInfo`) runs `vkGetSwapchainImagesKHR` (through us, into Streamline). On the images it gets back, which are Streamline's DLSS-G back buffers (`cloneFakeBuffers`), it builds image views, framebuffers, fences, semaphores and command buffers.
  - **The first matching present** initialises ImGui (`IMGUIManager::InitializeUI` → `VulkanBackend::InitializeUI`). That is the ImGui SDL backend, plus ImGui's Vulkan backend on queue family 0 queue 0: descriptor pool, sampler, pipelines in the game's pipeline cache, and the font atlas upload.
  - **Every later present** takes `globalResourceLock_`, which the extender's client-thread `IMGUIManager::Update` also takes. If there is draw data (`drawViewport_ != -1`), it also runs `presentPreHook`:
    1. It waits on a per-image fence on the CPU.
    2. It records a load/store render pass on the back buffer (`PRESENT_SRC` → `COLOR_ATTACHMENT` → `PRESENT_SRC`).
    3. It submits that pass to queue 0 of family 0. The submit waits on the present's semaphores and signals the extender's own binary `uiDoneSemaphore`.
    4. It writes that semaphore over `pWaitSemaphores[0]` in the game's array and sets `waitSemaphoreCount = 1`.
- Draw data exists only once a mod has used `Ext.IMGUI`. `ClientState::IMGUI()` (`Lua/Client/LuaClient.cpp`) creates the object manager lazily, and `IMGUIManager::Update` skips frames without it. **This install has no mods** (`modsettings.lsx`: GustavX only). So in the failing run, the per-present submit and semaphore rewrite most likely did not run. What did run: the patch on our three entry points, the per-swapchain resources on Streamline's back buffers, the one-time ImGui Vulkan initialisation on queue 0, and a lock in front of every present. A mod such as a mod-configuration menu would add the full per-present pass.
- **Not proven.** Which of these makes `sl.dlss_g` stop showing the generated frame is internal to Streamline. It keeps reporting 2 frames per app frame, but only one present per app frame reaches the next layer (`w_QueuePresentKHR`, `displayed/rendered 1.00`). Without the extender the same state lasts 3-5 s after each enable, then the ratio goes to 2.00. With it, the ratio never went to 2.00 in 35 s. The fix below removes every Vulkan-side effect of the extender except its loader-level `vkCreateInstance`/`vkCreateDevice`/`vkDestroyDevice` hooks. The foreground test decides whether that is enough. If the ratio stays at 1.00 with `se_unhook=1`, the cause is outside its Vulkan hooks (CPU timing, SDL hooks), and `se_unhook=0` vs `1` shows that.

**Options considered.**
- (a) Route the extender's present through Streamline "correctly": not possible. It already goes through Streamline; the problem is what it does before.
- (b) A separate pass-through present for the extender to patch: not possible with an inline patch. The game and the extender resolve the same dispatch-table entry, so whatever the extender patches is the game's present.
- (c) Switch off its ImGui renderer: there is no config switch. But the renderer only initialises from inside the present hook, so a present hook that never runs leaves the whole overlay inert. Its swapchain hooks then have nothing to do either.
- **Chosen: (c) from our side.** `layer/hookguard.cpp` snapshots the first 32 bytes of our `vkQueuePresentKHR`, `vkCreateSwapchainKHR` and `vkDestroySwapchainKHR` at instance creation. That is before the extender can patch them, since it patches after `vkCreateDevice`. At the game's `vkGetDeviceQueue` (before any present) and on the presenting thread every 64 presents, the guard compares the bytes, follows the jump chain (Detours: `E9` → stub → `jmp [rip]`) to the owning module, and writes our bytes back when that module is the Script Extender (`se_unhook=1`, the default). The extender's trampolines stay allocated but are never reached. Its overlay never initialises, so Lua `Ext.IMGUI` windows do not draw. Everything else in the extender (Osiris and Lua scripting, stats, ECS) does not touch Vulkan and is unaffected. `se_unhook=0` only logs (to reproduce); `se_unhook=2` restores patches from any module.
- Unit test: `bin/hookguard_test.exe` applies Detours-shaped patches (`E9` + `jmp [rip]`, `mov rax; jmp rax`, a detour in another module) and checks detection, attribution and restoration.

**Confirmation needed (foreground, extender installed).** Log lines:
- `[compat] entry-point guard armed (3 functions, se_unhook=1); BG3 Script Extender LOADED`
- `[compat] vkQueuePresentKHR: our entry point is inline-patched ... in BG3ScriptExtender.dll (BG3 Script Extender); seen in vkGetDeviceQueue`, followed by `original bytes restored` (likewise for the two swapchain functions)
- after `slDLSSGSetOptions(mode on)`: `displayed/rendered ... 2.00` within a few seconds, as without the extender.

## Known issues

2026-11-06 offline review (`review-dlss5`): deferred device initialization now checks
and consumes its pending scratch under one mutex, including the one-time trigger log.
Device teardown clears the SDK initialization flag, instance, requirements, optical-flow
queue flag and pending scratch along with the function table. A replacement device on
the same instance therefore skips Streamline instead of calling a null SetVulkanInfo;
a fresh instance can initialize it again. The actual teardown body is covered by
`layer_init_failure_test.py` (fails against the previous body, passes after the fix).
No live FG/HDR or recreation qualification is claimed.

- **Refresh-capped output.** DLSS-G paces to the 120 Hz refresh, so in this setup FG lowers the rendered rate instead of raising the displayed rate. To gain motion smoothness beyond 120 Hz, use a higher-refresh mode or VRR.
- **Loading screen.** DLSS-G switches on during the "100%" loading screen. The scene renders behind it, so every input looks valid, and `fg_menu_detect` does not catch it. This is harmless, and the transition into gameplay is handled.
- **UI blending space.** The game composites the UI in linear light: the UI is sRGB-decoded, and the PQ-decoded scene is attenuated by (1-a)^2. DLSS-G assumes `final = ui.rgb + (1-a)*hudless` in a single space. For UI detection this does not matter. For `fg_ui_recompose=1`, semi-transparent UI could come out with the wrong brightness, which is why it is off by default.
- **HDR10.** The test runs used SDR output, because the TV was asleep at start (swapchain format 44, B8G8R8A8). The HDR10 path is also handled: the composite fs `046c6949` is classified the same way, and DLSS-G supports RGB10A2/HDR10. It has not been run with frame generation on yet.
- **Motion-vector sign and camera matrices.** These are derived from the shaders, not yet checked with camera motion. A wrong sign would show as ghosting during pans. Flip it with `fg_mvec_scale_x/_y=1`.
- **Game's DLSS SR on the shared NGX instance.** SR runs, but it now uses the SDK's `nvngx_dlss.dll` (Streamline's plugin folder) instead of the game's own copy.
- **VSync.** VSync is not supported with DLSS-G on Vulkan ("VSync interval 1 not supported with FG, clamping").
- **Exit.** One run logged an access violation in bg3.exe while the game was being force-closed after a hang. Older builds had this; it has not been seen since the hang fix.

## Next steps

1. **Measure frame pacing.** Use a high-refresh or VRR display (or uncap the game) and a PresentMon/FrameView capture for `MsBetweenDisplayChange`. Capture a gameplay frame with `fg_snap`.
2. **Check motion.** Pan the camera with WASD (auto-run could send keys) and inspect `fg_snap` frames for ghosting. That settles the motion-vector sign and the camera matrices. Also try `fg_only_interpolated=1`.
3. **Run HDR10.** Run FG with HDR10 output and look at UI recomposition with a UI-alpha tag (single channel).
4. **Track A.** RR should go through the shared NGX instance by hooking the same `NVSDK_NGX_VULKAN_*` imports that `streamline.cpp` already patches, and swap SR for DLSS-D (RR) there.
5. **Dynamic MFG.** Streamline does not support it on Vulkan yet (driver says "missing components"). Revisit when it does.
6. **Clean up.** Drop the `sl_log_barriers` diagnostics once they are no longer useful. Consider `slFreeResources` when fg is set to 0 at runtime.

## FG x3 pacing review - 2026-11-06 (offline only)

See [the session analysis](evidence/fgx3-analysis.md) for the exact log timestamps,
code pointers and ranked proven/plausible causes. Most SDK interpolation pauses
explicitly follow focus loss; SyncInterval 0/1 accompanies the state transition
and does not establish its cause. Vulkan FG VSync is unsupported by this SDK.
The existing rendered-frame Reflex ceiling is correct for static MFG.

The present path now samples DLSSGState once per app present and reuses it for
input retirement and reporting. SDK counts accumulate over reporting windows;
they are not inferred by multiplying batched present calls. Failed tag/constants
publication invalidates FG readiness. These repair code defects; the supplied
session does not establish either defect as the cause of the reported stutter.
Release cadence summaries include SDK/proxy ratios, pause/focus log notices and
CPU destroy-wait total/max ms. Required consumption fences remain intact.
No new settings, shader variants or ABI changes. Runtime qualification is pending.
