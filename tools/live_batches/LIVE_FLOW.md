# Offline live-batch preparation

Jobs and plans belong under `tools/live_batches/<batch>`, never a watched inbox.
Write `predicted.json` before generating jobs. Combine phase groups with
`combine.py` into one launch per save; use a separate short GPU-AV launch.
Startup-latched settings must agree across combined phases. Do not combine
validation timing with performance windows. Hold Q/E for turns; use wheel for zoom.

Run the batch's `dryrun.ps1`. It samples every camera phase, checks trace markers,
window roles, scene-wait/capacity lints, budgets, and fake-queue positive/negative
paths. These receipts have no live authorization. Run Python synthetic tests too.

Only the serial queue may launch a game, with fresh authorization. An offline
dryrun, missing observables, or a draw-count log never grants a quality pass.
Every ON window and baseline character must be represented in the analyzer input.
Keep raw capture provenance; report FAIL/INVALID per check in JSON and Markdown.
