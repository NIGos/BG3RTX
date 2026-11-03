# Native hair cost2 batch (prepared, NOT authorized)

Predictions are in predicted.json. No game is launched by make_job.py or dryrun.ps1.
User instruction forbids commits in this task; the coordinator must commit predictions before a live run.

Four independent jobs: Water Queens House (S8), Test Dialogue1, Test Dialogue2, Test Dialogue3.
One launch per scene, Ultra qualified settings plus native hair/scalp/physical-origin startup latches.
The old GI-cull shortcut stays OFF: its +9..20% character brightening failed equivalence.

Each scene warms all candidates, then takes same-run cards and full-hair references.
Full native hair brackets every pool, cheap, mean and combined candidate independently:
full / pool / full / cheap / full / mean / full / visibility / full / combined / full.
Each state has a float-preview trace and a separate diagnostic-free GPU-cost window.
Every phase explicitly restores all candidate keys. No captures are written into a watched inbox.

- rt_hair_pool_trim: retire stale snapshots and pack eligible ordinary holds 88 -> 24 bytes/vertex; native startup only.
  Every pool/combined ON window requires positive packed jobs and zero unavailable builds.
  Judge used MB: reserved pool capacity can stay high after memory is reclaimed.
- rt_hair_cheap: one texture sample for physical non-primary coverage; first-hit dedicated hit-AO.
  AO distance weighting can change, so energy must pass. Joint GI/AO radiance rays retain closest search.
- rt_hair_gi_mean: stored empirical per-draw coverage, probabilistic GI occlusion.
- rt_hair_visibility: certified mapped static-draw subset; near/large/unknown sources retain native.
  At most one projected pixel along each axis plus wind and FP32 envelopes; near radius from GI settings.
  Log eligibility/cards/unknown. Zero eligible draws or cards means no demonstrated selection benefit.
- combined: all four. Instanced/pulled/GPU-written character selection remains open (docs/NATIVE_HAIR.md).

Scene-build total GPU ms includes mean precompute and is required in every cost window.
Report that cost alongside lighting savings so precompute is never hidden.
Cost is judged separately on complete 300-frame rt passes and gi GPU ms lines in EVERY cost window.
Report ON minus its neighbouring full-hair repeats and the repeat difference as noise.
The keep rule is total native-hair overhead <1 ms against same-run cards, not merely a saving.
Energy uses float pre-post scene colour: character pixels (RT2 material class) and whole-frame mean.
Every candidate must equal full-hair energy within the OFF-repeat noise; do not substitute PNG brightness.
Mean-coverage construction is an approximation; its Bernoulli expectation is not a character-energy proof.

Every measured window gates own-frame scene/no timeouts, no background builds, valid hair bounds,
no dropped capture, and water participation at S8. Pool-full is forbidden, especially Dialogue3.
No builds or offline shader tools may overlap future live cost windows.

Timing configuration generations discard mixed-state and delayed GPU samples after every toggle.
Plans are 561 seconds per scene (300-frame timing lines require 25-second windows), about 45.4 minutes
including four estimated 120-second loads. Forty-eight PNG/float-preview traces budget 1384 MB total.
This exceeds the usual 2-5-minute scene target to bracket four independent keys and their combination.

Generate: python tools/live_batches/hair_qual/make_job.py
Offline fake-queue check: powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/hair_qual/dryrun.ps1
Dryrun uses TEMP outside checkouts; missing real expect lines must produce INVALID in its negative run.
Jobs remain in this tools folder until explicit user steering authorizes a serial queue batch.

Hair-cost3 repair: the mean scan now examines at most 64 primitives per draw;
invalid/capped discovery uses full coverage. Probability and per-draw cap count
must fit the owning allocation. Mean windows are retained for requalification.
HQ4 uses hq_cost3_trim_plan.json: pool trim stays ON from load through every
reference and restore phase, because Dialogue3 reached 2047 MB before warm-pool.
HQ4 is no longer a trim OFF/ON comparison. HQ1-HQ3 retain that comparison.
Live 10:18-10:32: all four runs lost the device after mean ON; no mean gate passed.
Pool trim is proposed for default promotion separately; cheap gains were within
noise, so drop cheap from the shipping proposal (diagnostic stays off).
