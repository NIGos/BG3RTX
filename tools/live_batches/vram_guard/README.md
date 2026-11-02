# vram_guard: vg2 bundle candidate (prepared, not queued)

One launch at Water Queens House, custom Ultra + FG 2, visible_unfocused_fg.
The simulated 16 GB card budget is 15640 MiB. Every guard-on phase also sets
`rt_vram_compact=1,rt_capture_elastic=1`; OFF phases and restore set all three to 0.
The job requires the merged mesh-pool and capture-reserves layer DLL, then a restart.
Capture-reserves currently documents a continuous-capture retirement limitation.
This batch is prepared for that merge, not a live qualification or authorization.

Edit `make_job.py`, then regenerate jobs/plans. There are 14 phases, 206 s plus load,
three PNG traces, about 190 MB. Warmup/lower camera, measured ON view/turns,
static OFF/ON/OFF energy shots, OFF turns, 25 s `recover-settle-on`, 20 s measured
`recover-on`, restore. The separate settling phase permits the 254-frame pool
watchdog, 72-frame cascade observation, and GPU retirement at 30 fps; failure to
finish retirement remains a failure, not grounds to waive the recovery gate.

`expect_every` requires within-budget process usage and mesh/BLAS accounting in
all measured ON windows, no Streamline >100 ms or fence timeout there, and scene
wait 0 timeouts in long windows. Recovery additionally requires at least three
complete lines with a published pool repack and 0 pool copy/retirement MB.
The 6 s energy shots cannot hold complete 120-frame stats windows at 30 fps.
`expect` requires pool repack, cascade/RT elastic demand and successful replacement
logs: a target waiting for retirement alone does not pass. Counters may be zero
when there is no eligible BLAS evacuation; inspect reserved/live/pending and freed
bytes rather than assuming every chunk is movable. RT drops remain a report:
the simulated card cap may still be too small, and singleton retirement may block.
No invented periodic replacement counters are gated: current capture code prints
demand/target/waiting and successful retired-slot replacement events.

Predicted steady reserves (`predicted.json`): mesh 934 MiB (467 x 2), cascade
224 MiB per buffer / 672 total (169 x 1.25 rounded to 16 MiB). BLAS cap 2102 MiB
(2048 in whole 64 MiB chunks); RT capture cap/floor 1024 MiB at this simulated
budget; hit-material recycle cap 254 MiB, yielding toward 0 under pressure.
All maxima are not guaranteed together: with vg1 rest 10300-11300 MiB these sum
to 15232-16232 MiB, so the high-rest case needs another 592 MiB saved. Require
actual process usage <= 15640 MiB after shrinking, including overlap until retired.
The measured ON windows are view-on-a, turn-on-r/l and recover-on; warmup,
lower-on, shot-on and recover-settle-on enable the bundle without a budget gate.

After the run, compare pre-post scene-colour float means in dumps.json using
`tools/m4_promotion_gate.py`: shot-on must match the OFF pair within 0.92%
(2 x S8 OFF-OFF sigma). Report GPU ms and capture drops separately; do not judge
frame time in unfocused mode. If rest alone exceeds budget, record the cause.

Offline validation (no game or real queue):
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/vram_guard/dryrun.ps1`.
