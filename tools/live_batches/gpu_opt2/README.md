Prepared only; never copy to a watched inbox without explicit batch steering.
Camp uses High, S8 Medium, fixed geometry throughout each launch; 1.0 / 0.5 / 1.0.
Cost windows are trace-free and diagnostics-free. Require complete 300-frame timing lines in every state.
Reject undersampled cost windows; extend them if rendered cadence is below 30 fps.
Static shots keep exposure held. Compare float pre-post scene luminance means from dumps.json,
using OFF repeats as the noise band; never use PNG/bloom energy. Preserve all nine shot summaries.
The scale key is restored to 1.0 before exit; the queue restores original settings afterward.
No GPU savings or real-scene quality qualification is claimed by this draft.

Coordinator analysis: `python tools/m4_promotion_gate.py --batch gpu_opt2 --pool run`.
Keep predicted.json before running; this task forbids commits, so the coordinator must preserve
its pre-run provenance when promoting the batch. Do not claim a committed prediction here.
