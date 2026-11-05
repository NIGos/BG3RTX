# CPU optimization 1

Offline candidate; default `cpu_opt1=0`. No game launch, queue staging or commit.
Shader request delta 0. ABI unchanged. Requires the diagnostic layer for hook timers.

## Existing cost evidence

Reviewed `opt-profile-inn.1.001709.log` and its runner in the main checkout's
`captures/queue/logs`. The full normalized table is [profile-inn.md](profile-inn.md).
The newer `005111` run lacks complete High windows; it is not a replacement for
the complete receipt. Existing window sums of 58-185 mean **ms/120 rendered frames**,
approximately 0.48-1.54 ms/frame, not milliseconds per submit or per frame.

`SubmitCpuTimer` converts QPC ticks to integer microseconds **per call**, then
`watchdog_report` divides the sum by 1000. Normalize by that same line's `presents`,
never `displayed` (FG doubles that count). The inclusive submit timer spans the
driver call and queue-lock wait as well as our hooks; it is not exclusive layer CPU.
Sub-microsecond calls truncate to zero. `cpu_timing=1` and diagnostics are required;
zero totals without those controls are unavailable measurements.

Inn period-summary medians:

| CPU receipt | Ultra static | Ultra pan | High static | High pan | Unit |
|---|---:|---:|---:|---:|---|
| Submit, driver included | 1.0373 | 1.4529 | 0.9948 | 0.9780 | ms/frame |
| Descriptor tracking, driver excluded | 0.0024 | 0.2810 | 0.0075 | 0.3168 | ms/frame |
| Alpha clone driver updates | 0.5062 | 0.5183 | 0 | 0 | ms/frame |
| Alpha clone calls | 2 | 2 | 0 | 0 | calls/frame |
| Capture hooks, all threads | 12.6225 | 15.3950 | 7.1600 | 6.5390 | ms/frame |
| Captured driver draws, nested | 0.2820 | 0.3220 | 0.1940 | 0.1820 | ms/frame |
| Hook bodies, native driver draw excluded | 12.3405 | 15.0550 | 6.9660 | 6.3570 | ms/frame |
| BVH per-draw BLAS keys, nested | 0.4965 | 0.7090 | 0.5955 | 0.7785 | ms/build |
| TLAS instances, nested | 0.6375 | 0.8435 | 0.5365 | 0.4310 | ms/build |

The minute-wide life receipts are 0.019 ms/frame over 1340 frames and 0.063
over 1673 frames. They include submit-side life mutex acquisition/map updates,
not all lifetime recording, fence polling or destruction waits. Do not assign
those averages to individual short windows.

Scene recording reports pan BVH 3.325 ms/build, alpha commit 0.181, enclosing
capture-end-to-commit 3.686 (High: 2.366, 0.186, 2.571). They are nested inside
capture pass-after. Static windows lack a wholly contained 300-build receipt
after dropping the first straddling line. Scene-build GPU timestamps are not CPU.
Capture totals already include native-hit prepass work. Alpha clone updates
are also inside material-query work. No exclusive total or critical-path CPU
time can be reconstructed by adding these overlapping, differently scoped rows.
The measured hook bodies alone clearly exceed the 0.1 ms/frame stop threshold.
The driver-excluded row subtracts total minus driver draw within each individual
summary before taking the median. It still includes our injected Vulkan command
recording, locks and diagnostics overhead; it is aggregate thread work, not the
render-thread critical path or a measurement against an unmodified game.

The same parser reviewed S8 `000944`, camp `001324` and Dialogue1 `002108`
logs/runners. Across their static/pan period-summary medians (separate scenes,
not A/B), submit is 0.338-0.919 ms/frame and native-driver-draw-excluded capture
is 3.050-9.914 ms/frame. Ultra alpha clones cost 0.497-0.573 ms/frame; High has
zero clone calls. Descriptor tracking ranges 0.0009-0.0869 ms/frame. These
confirm the CPU work is not negligible beyond the inn too.

## Change and proof

`bvh_build` holds `g_mu` throughout. Under the key, retain four vectors of owned
indices, keys, references and flags between invocations. Clear before each build,
including after early returns. No command-state pointer or GPU lifetime is cached.
Replace the two membership-only temporary node sets (present/first dynamic draw)
with retained contiguous tables. Neither table is iterated: original draw order,
first duplicate winner, held-draw bypass, failed allocation and all Vulkan commands
stay unchanged. Zero and UINT64_MAX remain valid keys. Membership is cleared on
every build; no invalidation dependency spans frames.

The per-draw `rt_hair_pool_trim` lookup uses the existing present-refreshed atomic
settings mechanism only when the key is on. Its old fallback 0 disagrees with the
table's 1, so each old lookup also formats a literal and enters `cfg_check_literal`'s
mutex. The new hot-table entry uses the correct default 1; explicit values still
win. OFF preserves the original lookup and allocation path.

`cpu_opt1_test` compares insertion and membership against `std::unordered_set`
for 12004 adversarial/random entries, resets, smaller scenes, held draws and
failed-build duplicate suppression. It verifies no table growth after warmup.
The 5000-key lifecycle benchmark (400 builds, insert plus membership probes) gave
OFF 0.3183 / ON 0.0517 ms/build with identical receipts; this is synthetic host
work, **not a live game saving**. The analyzer fixture checks actual-present
normalization, FG exclusion, zero-call alpha and rejection of straddling receipts.

## Prepared live decision

`jobs/opt-cpu1-inn.json`: one Ultra inn session, OFF/ON/OFF/ON static windows of
600 frames each. ON storage is warmed first. Readiness uses scene-ready, the
CPU-key settings witness and fresh zero-pipeline watchdog only; no shader or
pipeline generation waits. Producer cadence is 120 frames, diagnostics otherwise
off. The analyzer drops the first unreset BVH/300-build scene/matrix receipt.
Life bookkeeping remains minute-wide context; it is not the candidate's cost gate.

Expected scheduled work 104.8 s after load, hard cap 300 s including loading.
All input uses `visible_unfocused_fg`; no images/dumps. Lint passes; readiness
latencies are explicitly unproven. `predicted.json` records the hypothesis and
OFF-repeat-spread decision rule. Inspect every ON window and lifecycle forbids.
No savings, quality or live pass is claimed before the authorized measurement.

Validation: `scripts/build.ps1 -Test` passed 180/180 in 273.19 s (277 s including
build), with `BG3RTX_SHADER_CACHE` under this checkout's `.scratch`. The initial
shared-cache attempt was stopped and discarded. Settings defaults and diff
checks pass; final analyzer fixtures pass separately after the reporting update.
