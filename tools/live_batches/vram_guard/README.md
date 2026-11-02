# vram_guard: vg2 iteration 3 (prepared, not queued)

One launch at Water Queens House, custom Ultra + FG 2, visible_unfocused_fg.
Simulated budget: 15640 MiB. ON enables guard, compact and elastic; OFF and
restore disable all three. Layer rebuild and game restart required by the owner.
This preparation does not authorize a live run.

Edit `make_job.py`, then regenerate jobs/plans. The unchanged plan has 14 phases,
206 s plus load, three PNG traces, about 190 MB. It includes ON view/turns,
static OFF/ON/OFF energy shots, OFF turns, 25 s recovery settling and 20 s
measured recovery. Jobs stay here, outside watched pending inboxes.

Memory snapshots use event gates with 1 s settling. Every measured ON snapshot
must be within budget and include reserve accounting; recovery needs at least
three snapshots with a published repack and zero pool copy/retirement overlap.
BLAS and scene-wait summaries retain complete-window semantics. No Streamline
>100 ms or fence timeout may occur in measured ON windows.

Required capture events match production: `pending producer boundary` for a
target and `detached slot` for replacement. A target alone cannot pass.

The 21:38 vg2 log disproved the old pool eligibility prediction: 1050 / 2047 MiB
never passes quarter-full. Iteration 3 keeps 25% headroom and requires 25% savings,
retaining the peak target for 254 frames. Expected recovery pool: about 1312.5 MiB,
about 734.5 MiB saved after retirement. Cascade recovery was already 3 x 352 MiB.
Its final return to 3 x 544 MiB followed the restore phase disabling elastic;
that key-off restoration is expected and outside the ON gates.

Capture sizing now retains a rolling 24-sample peak across replacements, shrinks
only with another 25% capacity step free, and grows for actual demand exceeding
capacity rather than reserve-only pressure. Refusal logs report attempt count
and peak allocation: 25 retries of 960 MiB are not a 24000 MiB allocation.
RT capture has its own accounting category. Growth still asks for the full
replacement allocation because the old buffer remains alive.

Predictions in `predicted.json` are unqualified. Read actual memory including
retained buffers and report capture drops. Compare pre-post scene-colour float
means from dumps.json: shot-on must match the OFF pair within 0.92% (2 x S8
OFF-OFF sigma). GPU ms, energy and visual invariance need a later authorized run.

Offline validation (fake queue only):
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/vram_guard/dryrun.ps1`.
