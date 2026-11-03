# S6 box light art intent (not authorized)

One static-camera Adamantine Forge run. No queue files are installed by this folder.
Prediction: predicted.json, written before the job. Asset audit: asset_flags.json (read-only DB).

The 227 s plan toggles OFF / legacy ls_box=2 (ls_box_honour_shadow=0) / fixed
ls_box=2 (ls_box_honour_shadow=1) in one launch. Uses visible_unfocused_fg and FG 2.
Diagnostic windows gate every existing ls box trace line: old unshadowed traced > 0,
fixed unshadowed traced = 0. Cost windows have diagnostics and captures off.
Nine alternating shots hold exposure throughout and bracket every old/fixed shot with OFFs.
The restore phase sets ls_box=0, honours shadow flags and releases exposure.

Energy is an automatic post gate (post_gate=true), using energy_check.py and the existing
m4_promotion_gate scene-mean reader and Student quantile. It reads only the pre-post
5c3fdabc slot 0 float RGB mean from dumps.json. OFF repeat differences estimate sigma;
four comparisons share the existing 5% family false-alarm budget. Each fixed shot must
stay above its bracketing OFF mean minus the noise band. Both repeats must recover
energy toward OFF relative to old by more than twice that band. Unresolved recovery is
inconclusive and fails qualification. Nonfinite or missing scene means fail closed.
The global mean cannot prove local recovery: review the same-run PNGs at the artistic
lights, especially the lower-right walkway and central pillar. No invented ROI energy.
GPU cost remains to measure: compare local-light GPU medians in cost-old/cost-fixed
against cost-off-a/cost-off-b. Historical old-mode costs are reference only.

Offline: powershell -File tools/live_batches/box_lights_art/dryrun.ps1
Read-only recheck: python tools/live_batches/box_lights_art/energy_check.py <queue-log>
Use --captures-root if the captures moved. The queue also enforces normal expect/forbid,
scene-wait and capture-capacity gates. Estimated 550 MB, about 5.8 min including load.
The full Ultra preset is used; no preset promotes ls_box. Experimental rf_hit_gi is explicitly
off: its reflection hit box policy is unchanged in this deferred-only fix. Live quality/cost is pending.
