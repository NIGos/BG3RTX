# Independent r3 capacity review

Read-only review of the immutable r3 snapshot under
`captures/vegetation-regression-20261031/r3` in the omm-bake worktree.
Candidate 859b879. Inputs: bg3rtx.log, runner transcript, motion.json,
run-status.json and capacity-summary.json. Existing local `phase_metrics`
independently reproduces the reported phase medians. No game/GPU calls,
source/candidate changes, or access to the active second job occurred.

Status is done, plan_completed true, real foreground presentation, seven phases,
normal owned close requested, no exception or observed premature exit code.
The runner plan starts at 00:30:20.535 and ends at 00:36:50.532. Process runtime
is about 485 seconds including loading; the scene plan itself is 390 seconds.
FG generated/displayed ratios are approximately 2, SDK status 0 and no interior
host option toggles. Actual RT is present in measured phase windows.

The existing phase interior is start+15 seconds through end-10 seconds. The
following camera capacity windows all report 0.00 alpha draws made opaque and
0.00 opaque draws dropped per frame. Raw warnings were also searched: the only
camera capacity warning is before the scene plan, not hidden by rounded zeros.

| Phase | Camera windows | Cascade full maximum, draws/frame | FPS median | Primary match median | Aggregate RT GPU ms median |
| --- | --- | --- | --- | --- | --- |
| static-check | 29 | 20.7 | 53.3 | 97.4075% | 7.438 |
| slow-right | 8 | 21.1 | 45.0 | 97.1545% | 8.303 |
| slow-left | 8 | 20.9 | 43.2 | 96.7660% | 8.621 |
| fast-right | 8 | 20.4 | 48.1904 | 96.0680% | 7.909 |
| fast-left | 8 | 17.4 | 50.4 | 96.3905% | 7.693 |
| static-cost | 32 | 20.7 | 59.8940 | disabled | 3.824 |

Warmup interior has two zero-overflow camera windows, aggregate RT 3.867 ms;
its FPS/primary diagnostics are unavailable and are not invented. The diagnostic
phase cost numbers include primary/epoch work; only static-cost has them off.
The aggregate RT timer is not isolated Sun/Hair build cost.

Global camera forced-alpha maximum 0.62 draws/frame comes from the first
120-frame report at 00:29:53.383, before the plan. The raw bootstrap warning at
00:29:33.982 records 64 MB alpha reserve versus 307.8 MB demand and 74 draws / 243.8 MB
made opaque on that frame. Thus steady camera allocation is qualified in this
scene, while bootstrap still overflowed. Global opaque drop maximum is 0.00.
Do not call the complete run zero-overflow or claim all initial frames were fixed.

Cascade full maximum is 22.0 draws/frame globally and remains nonzero in every
measured phase. Consequently only steady CAMERA capacity is green; combined
camera/cascade capacity is not. The separately authorized 587 MB cascade run is
not reviewed here and no result is assumed.

No deciding-window API/RR/device errors were detected by the existing parser.
However the full log is not literally fault-free: loading reported no present
for 5 seconds at 00:29:39.580, diagnostic pending build records, and
`vkGetDeviceFaultInfoEXT (counts) failed: -13`. Presents resumed at 00:29:48.586,
before the plan. Record this recovered startup stall rather than calling it a
crash/device loss or suppressing it with a blanket no-fault claim.

Verdict: steady camera-capacity invariant supported; cascade capacity open;
startup bootstrap/stall limitations retained. This is NOT full P1/M1 or visual
vegetation qualification. Static 97.4075% and moving 96.068-97.1545% primary match
do not satisfy the intended approximately 99.5% static-level gate. No paired
same-run old-capacity control or automatic face/distant ROI proof is available.
