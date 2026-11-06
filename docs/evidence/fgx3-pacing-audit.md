# FG x3 pacing contract audit (Round 2, offline)

Code pointers in the audit refer to `4ddec20f` before the Round 2 edits. SDK guide
pointers refer to the bundled Streamline 2.14.16 documentation, not an assumed
D3D12 contract. This audit separates a requirement from an optional optimization.

## Swapchain, queues and metering

- `layer/streamline.cpp:1901-1910` forwards the game's creation info through the
  Streamline proxy; it does not choose a special x3 present mode. The supplied
  `sl.log:522,531` requests 3 images and the SDK uses 6; `bg3rtx.log:132,134`
  records 3840 x 1600, format 64, present mode 2 (FIFO), images >= 6. There is no
  MFG-specific minimum-image formula in guide sections 6.2 or 16. The SDK owns
  its real swapchain and fake buffers. Changing the game's requested count to
  multiplier + 1 would not be a justified fix to an observed shortage.
- The game submits/presents on `000002BD7BA714D0` (`bg3rtx.log:137,154`);
  SDK downstream presents use `000002BD7BA728D0` (`:836-851`). Reserved queues
  are graphics family 0 index 1, compute family 2 index 1 plus a second queue,
  native OF family 5 index 0 (`:71`). This meets the manual-hooking guide
  5.2.1/5.2.2 queue separation rule. `sl_acquire` forwards semaphore/fence and
  `sl_present` forwards the entire VkPresentInfoKHR, including wait semaphores;
  wrappers do not strip the acquire/present GPU dependencies (guide 16).
- `sl.log:489` says `VK_NV_present_metering available`. The SDK attaches
  VkSetPresentConfigNV to all six logged downstream calls (`bg3rtx.log:836-851`),
  which are at 15:03:46 while SDK interpolation is **disabled** (:604). Their
  batch size 1 is therefore not proof that enabled x3 lacks metering.
  `sl.log:638` reports `FC feedback: 1` at 15:06:38 and :641 reports good feedback
  at 15:06:40. The exact failure meaning is not exposed by the public guide.
  There are no per-enabled-window batch/feedback samples in this session.
  The Streamline guide contains no host-side metering enable switch. However,
  the public Vulkan API requires the device extension **and**
  VkPhysicalDevicePresentMeteringFeaturesNV.presentMetering enabled at device
  creation. Our baseline SlDeviceScratch/device feature setup has neither
  (streamline.cpp:1508-1720); the final extension log :75 and original feature
  chain :41 contain neither metering extension nor structure 1000613001. NR's
  device wrapper adds neither (dlss5.cpp:360-413). This is a **proven public API
  enablement omission**, with **unproven pacing consequences**: SDK feedback
  can come from an undocumented private driver path. The plugin's
  dlfgPresent/flipMetering source is not in the public SDK tree.
  [Vulkan feature enablement](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePresentMeteringFeaturesNV.html),
  [device extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_present_metering.html).
  Round 2 adds off-default cold candidate fg_present_metering, querying support
  and enabling both public extension/feature together; it leaves SDK batching
  intact. Unsupported configurations keep the baseline path. The extension is
  provisional, so neither default promotion nor a guaranteed pacing fix is
  justified. Bounded batch/feedback counters expose runtime outcomes.
  The batch parameter covers the next N **separate present calls**; it is not
  a multiplier for the proxy's present-call counter.
  [Vulkan batching contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSetPresentConfigNV.html).
- Vulkan FG VSync is explicitly unsupported (`sl.log:486`, guide 22:1159).
  The repeated clamp warning starts at :605 and reaches occurrence 4096 at :663.
  This is an unsupported requested configuration, and a plausible pacing factor
  while focused; it is not proof of the seven long gaps or the x3/x2 difference.
  The SDK internally prints SyncInterval 0 while enabled despite the FIFO input.
  The integration has no DXGI SyncInterval argument to correct per Vulkan present.

## Requirements checklist

| SDK requirement / reference | Integration and status | x3 relevance |
|---|---|---|
| OS/driver/HWS and feature requirements (guide 1, 3; manual 5.2) | `sl_init` gathers required extensions/features/queues, device setup supplies them. Log :30-32 identifies driver 621.14, OS 26300; plugin loads and reports supported. | No unmet machine requirement is logged. |
| Initialize before Vulkan and use manual proxies (guide 2, 4; manual 4-5) | Preferences use manual hooking and frame-based tags (`streamline.cpp:1340-1355`); swapchain/acquire/present/idle proxies and surface HWND registration are used. | No bypassed game present is established. |
| Metering public API setup (Vulkan feature/extension contract) | **Gap:** baseline manual setup omits extension and feature bit, despite runtime availability. Candidate fg_present_metering=0 enables both only when supported, at restart. | Concrete setup omission; SDK private-path feedback prevents causal attribution without live evidence. |
| MFG capability and count (guide 6.2:525-537) | State capability queried/logged at :1838-1841. Requested x3 maps to 2 generated at :2473; actual max is 5 (log :127, sl :488). **Gap:** count is clamped to 4x, not runtime max. | Session count 2 <= 5 is valid; generic support gap cannot explain this case. |
| Options ordering (guide 6:487-495) | SetOptions immediately before proxy present, same thread, on actual mode/flags changes (:2584-2622). | Live x2/x3 change is the documented API; no guide requirement to reset/recreate solely when changing multiplier. |
| Tag required buffers at viewport 0 (guide 5, 17) | Depth/MV/hudless/UI and backbuffer extent tagged at :2537-2562. Private depth/MV selected from matching submit/camera. | No x3-specific tag schedule; publication failures are gated by Round 1. |
| Valid lifetimes/layouts (guide 5.2, 15.1; manual 6.2) | Persistent input copies and submission-ordered observed layouts; old image destruction uses global fence. **Unqualified:** shader/input correctness and each colour's final use are not proved for x3 + NR. | Global-fence retirement can unnecessarily couple old game images to new FG work; Round 2 candidate addresses destruction only. It does not permit early modification. |
| Null invalid tags, FG Off while invalid/menus/transitions (guide 5.2:351, 17:946,953) | Missing/stale snapshot disables FG; menu detection flag set. **Gap:** invalid frames skip tag publication instead of explicitly nulling every cached tag. The layer relies on proxy handling teardown; it does not explicitly switch mode Off before every game window manipulation. | Known loading discontinuity :1135 is separate from uninterrupted in-game x3. No related error is proved in the enabled case. |
| Common constants once, matching present token (guide 7, 8, 17:949-952) | One SetConstants at :2562; same token for PresentStart/End at :2627,2634. **Gap:** sent late at present, not as early as possible (guide :658). | No common-constants-missing warning or duplicate constants is recorded in this session; no x3 multiplier-specific token advance is required. |
| Correct camera/reset data (sl_consts.h; guide 20) | `fill_constants` supplies depth inversion, MV camera inclusion, scales, unjittered matrices and camera basis. **Gap:** reset is always false (:1100), rather than propagating the game's camera-cut/reset. SDK ASCD is enabled by default but requires orthonormal basis (guide :1054-1068). | Can harm scene-cut reconstruction; does not prove steady x3 pacing stutter. Existing x2 input validation does not qualify x3. |
| renderingGameFrames member (guide 17:950) | **Not actionable:** that member is absent from the supplied sl_consts.h Constants version 2. | Stale guide text must not be mistaken for a missing field in this build. |
| Reflex/PCL every frame and proper placement (guide 8; Reflex 5,8; PCL 3) | Sleep + SimStart/SimEnd/RenderStart in new_frame (:1024-1036); RenderEnd/PresentStart before present and PresentEnd afterward (:2625-2634), same frame token, regardless Reflex mode. **Gap:** synthetic zero-length simulation markers do not bracket actual game simulation; RenderStart precedes the whole next frame and early host present diagnostics are outside the PresentStart bracket. No full Reflex Test Utility qualification is recorded. | Timing/latency quality is unqualified; marker order alone is correct. No logged marker threading race is proved. |
| Supported VSync only (guide 22) | **Unmet in user configuration:** game FIFO/VSync remains requested although SDK capability says 0. The layer logs capability but does not control the game's VSync UI. | Plausible focused pacing issue; unmeasured. No forced override ships in this fix. |
| Check state status/minimum dimensions (guide 17:956-959) | State logged; Round 1 queries once after every present with nullptr options. **Gap:** nonzero status/minimum-size result is not a fail-closed gate. | Session resolution exceeds minimum and no failure status is established; not a demonstrated cause. |
| Queue input-consumption synchronization (guide 15.1,16) | Default eBlockPresentingClientQueue; g_px acquisition/presentation semaphores forwarded. CPU destruction waits protect inputs. **Optional optimization not adopted:** eBlockNoClientQueues requires additional modification synchronization on every queue. | Default serializes the client's presenting queue until FG GPU processing completes (sl_dlss_g.h:57-65). More generated work can reduce base throughput, but its actual cost is not timed here. |
| Recreate swapchain on On/Off (guide 18) | **Gap:** user/temporary mode changes do not force game swapchain recreation. | Mainly Off-path extra-copy overhead; x2 to x3 remains On and has no such requirement. Recreating on every temporary gate would be a broad unsafe change. |
| Dynamic resolution (guide 10) | Dynamic flag/target size not set; fixed-resolution case. Render-resolution changes are handled as game discontinuities. | Dynamic MFG not used, and unsupported here. Static 2560 x 1067 case does not establish dynamic-res failure. |
| HDR/UI blending and matching extents (guide 5:265-269) | Composite resources have output-sized extents; existing docs explicitly leave PQ UI colour recomposition unqualified (`fg_ui_pq=0`). | Potential image-quality gap, not proof of long CPU frame gaps; both x2 and x3 share it. |
| Motion blur reduction, debug input tests and release QA (guide checklist/17/21) | No layer motion-blur adjustment; old tests cover x2/x4 and input copies with limitations. **Unqualified:** x3 + Ultra + NR enhanced debug/pacing tests. | Do not call x3 qualified from old SDK state-success lines. |
| Production signed binaries, certificate validation (guide 2.1.1,17) | Production library selection is implemented; no sl.interposer custom-certificate validation is implemented in streamline.cpp. NR runtime trust checks are separate. | Integration completeness gap, unrelated to observed multiplier pacing. |
| Shutdown before destroying Vulkan objects (guide 2) | Shared NGX/process-lifetime retention for NR intentionally avoids runtime shutdown; device recreation is constrained by that design (TRACK_DLSS5). | Known lifecycle limitation, not per-frame x3 evidence. |

## Other paths and cost attribution

`layer/ngx.cpp:1479-1531` records NR immediately after successful RR, before
rrpost temporal/final and the later game's UI composite. FG discovers its hudless
colour at UI composite (`streamline.cpp:2135-2208`), then publishes it at submit
(:2345-2354) and present. NR is therefore before hudless discovery, not a new
CPU/GPU stage inserted between an already captured hudless frame and present.
The provider uses the same primary command buffer and no extra host submit or
per-frame CPU wait (`dlss5.cpp:239-323`, TRACK_DLSS5 section 3). NR adds real GPU
work to both x2 and x3; these release logs omit per-pass NR/FG GPU timings. It
can compound serial FG scheduling, but an NR-to-x3-specific stall is unproved.

Boost is not necessary for the bad x3 window: seven >100 ms gaps occur at
15:07:20-23; Boost is first applied at 15:08:44.037 (bg3rtx.log:3816). fps_cap
was already 0 at 15:07:10.572 (:2770). The later x3 section also uses Boost, but
that does not isolate a Boost interaction. Reflex guide 8 allows some FPS hit
from On + Boost; no extra multiplier scale or second Sleep is required for MFG.

The SDK's reference cost table (guide 1.1:57-68) lists RTX 5090 4K FG GPU costs
of 1.72 ms at 2x and 3.32 ms at 4x. It supplies no 3x/3840 x 1600 bound and is
not a measurement of this workload. It cannot explain or exclude the observed
~10 ms differences between uncontrolled native frame periods. In particular,
input-consumption completion is not proof of scanout completion: the public
fence contract ends when inputs are no longer used, not after two generated
frames have both been physically displayed. ~40 ms destroy-log spacing gives
no positive lower bound on the duration of any one CPU wait. Next-session
CPU wait/SDK proxy/query/sleep timing and actual display-change cadence are the
deciding evidence; neither a proxy present ratio nor SDK count alone measures
visible stutter.
