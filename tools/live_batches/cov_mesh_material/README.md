# Pak mesh materials: blocked offline, not authorized or queued

These four draft jobs use one launch per save (S8, S6, inn, camp), static OFF / ON / OFF
windows and `visible_unfocused_fg`. They must stay outside every watched queue inbox.
`predicted.json` was written first. The current implementation lacks a proven mesh join,
native basis and row/texture binding, so no coverage or timing gain is predicted and these
jobs are **not READY_FOR_LIVE**. Complete those dependent offline checks before requesting a batch.

The shared runner schedules time windows: diagnostic windows are 20 / 25 / 20 seconds,
cost windows 20 seconds each, and three single-frame traces supply OFF / ON / OFF scene
colour. The diagnostic gates require a line covering at least 120 completed frames in
every window. A short or absent line makes that window INVALID; extend the phase if needed.
`rt_stats` is disabled during all cost and image windows. `hold` is 120 seconds.

Read qualified share Q / A in each diagnostic window. ON must improve beyond the OFF
repeat spread. Read GI trace GPU medians in each cost window and report ON minus the
OFF mean, even when frame time is capped by focus. Capacity overflow and scene wait
timeouts must remain zero in every gated window. Existing gates cannot identify which
qualified rows changed: the offline preservation check must prove zero changed bytes
and evaluated values on the previously qualified subset before this batch can qualify.

Energy is the float pre-post scene-colour mean in each trace's `dumps.json`, never a PNG
or bloom mean. Compare ON with both OFF repeats and derive its band from their noise.
Global energy equality does not prove equality on the formerly qualified subset.
The final phase restores `rt_hit_material_paks=0` and releases exposure hold; the queue
runner also restores the original INI. Inspect fallback participation counters once the
runtime exposes them and add an every-ON-window mapping gate before requesting execution.

Offline validation: `powershell -NoProfile -ExecutionPolicy Bypass -File
tools/live_batches/cov_mesh_material/dryrun.ps1`. This exercises a fake runner and temporary
queue only. Estimated batch: 4 x 174 seconds plus loads, 12 single-frame traces, below 1 GB.
No game has been launched and no permission receipt exists.

`offline_inventory.json` inventories the five read-only source packs and fingerprints
all 659 active captured/pool rows (114 / 52 / 174 / 185 / 134). It records historical
replay aggregates separately from current preset shares. Existing camp/inn `rtstats.bin`
files have zero HM_BLOCKERS counters, and S8/S6 have no such file, so this inventory
cannot rank missed mesh families by actual hit count. The report does not claim a
candidate row or energy pass. `tools/mesh_material_replay_audit.py --candidate-root ...`
can reject changes to any previously active row at its original table address.
