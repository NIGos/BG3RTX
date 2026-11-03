# Compact catalogue join diagnostic

Unqueued camp job; 75 s static plan plus one load (~3.3 min), no captures,
no expensive RT statistics, estimated logs 10 MB. Requires explicit live batch
authorization; this folder grants none. Never launch the game for offline checks.
The startup settings enable rt_mesh_content_id=1 and rt_hit_material_paks=1.
Two ON observations surround OFF; restore disables paks. Existing queue settings
restoration still applies. Uses visible_unfocused_fg and never requests focus.

Predict loader=40,983 candidates. Positive eligible draw attempts and all rejection
counters must be logged in each ON observation. UV candidates and published rows
may be zero; that is a diagnosis, not an invalid run or a quality pass. Read the
existing paks line for full matches, records and ready basis. Resource, COLOR,
cull, shader recipe and full witness checks remain mandatory. No image-energy
or GPU-cost claim is made; this job cannot promote the key.

Offline only: powershell -NoProfile -ExecutionPolicy Bypass -File
 tools/live_batches/cov_paks_compact/dryrun.ps1

The shared harness creates marked synthetic queues under the temporary directory,
never a watched inbox. Stage jobs/plans only after the coordinator authorizes a
concrete batch. The loader reads the additive compact manifest beside assetdb.
