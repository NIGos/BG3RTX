# Pak material coverage batch (cov-paks)

Three unqueued jobs: S6 Adamantine Forge lava view, Campsite - 27h, and S8
Water Queens House. Never run while the user plays. This folder grants no live
permission. Status remains BLOCKED_OFFLINE until real generated joins, fallback
publication and previously qualified row preservation pass their offline checks.

Each save has one launch, static OFF / ON / OFF inside the same run, Ultra's
qualified keys on, FG 2, and visible_unfocused_fg. Plans are 174 seconds per save;
estimate 14.7 minutes with loads, nine single-frame traces, about 570 MB.
Diagnostics run only in diag-* windows; cost and image windows disable them.
The startup key enables capture support; the final phase restores paks=0 and
releases exposure hold. The queue also restores the original INI.

Read `hit material (N frames)` in every diagnostic window (N >= 120):
- Missing-record share = 100 * `instance without a material record` / `attempted`.
  Retain numerator and denominator; ON reduction must exceed OFF repeat spread.
- Qualified share = 100 * qualified / attempted; require a gain beyond OFF spread.
- The two counters measure different subsets; never substitute base failures for
  missing records. A zero numerator has no measurable missing-record improvement.

Read float pre-post scene-colour mean from each trace's dumps.json for energy.
ON versus mean OFF-a/OFF-b must stay within their absolute repeat spread; PNG and
bloom means are invalid. Read GI trace GPU median from each cost window and
report ON minus the OFF mean. Every window must retain its expected log lines,
zero scene-wait timeouts and zero capacity overflows. A completed run without
these lines is INVALID. A qualified-row regression rejects the candidate even
when whole-scene energy passes. `predicted.json` records unknown deltas as null.

Offline check (no game): `powershell -NoProfile -ExecutionPolicy Bypass -File
tools/live_batches/cov_paks/dryrun.ps1`. This uses marked temporary fake queues
outside checkouts; none is a watched inbox. Static plan and gate checks pass; final fake-runner check passes both A and B.
Fake run A must reject all three jobs for missing real log evidence; stripped
run B must complete all three. Python launcher `py -3` is required by the shared
harness; this PC's dryrun used a temporary PATH adapter to installed Python.

Before an authorized live batch, stage plans under the test checkout's
captures/live_batches/cov_paks/plans and generate the local join table with
`python -m tools.assets.mesh_paks --help`. Do not ship generated pak data.
Lava/Tileset identity candidates do not establish their vertex-colour recipes.

Local generation completed: 30 GR2 -> 74 rows (40 HLOD, 13 lava, 21 Tileset),
zero decode failures. Ten rows carry the audited HLOD_GM recipe tag. Fixed runtime
file: data/assetdb/mesh_paks_cov_paks.bin beside the asset DB (76,109,292 bytes).
The companion JSON retains material/texture provenance; neither file is shipped.
Twenty-seven candidates use vertex colour; these remain unqualified. Native
ExportOrder/LOD fields are unavailable in this decoder; source/name conflicts
are rejected across the whole catalogue. Limited decoding cannot establish
uniqueness against every unseen GR2. Current per-draw resources and cull checks
remain mandatory. No positive live share or energy prediction follows from row count.
Every diag-on window requires exact matches, records and ready basis above zero;
a loader-only or zero-participation run is INVALID.
