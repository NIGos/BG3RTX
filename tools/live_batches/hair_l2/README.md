# Hair L2 batch (live_batch hair-l2), READY_FOR_LIVE, NOT authorized

Question: live L2 of docs/RTX_HAIR_COMPLETE_PLAN.md (M2 shadows, M3 image). With strands on screen, does strand
shadow transmittance (`rt_hair_groom_shadow_t`, C2) lift the skin and neck under hair without a brightness change
elsewhere, and do dithered strand tips (`rt_hair_groom_tips`, C3a) fade the tips without flicker or energy change?
What does each cost? Predictions: [predicted.json](predicted.json).

Jobs (`python -m tools.live_batches.hair_l2.make_job`): `l2a-dialogue1-fg2` (Test Dialogue1), then
`l2b-campsite-fg2` (Campsite - 27h). Two launches, one per save. Shared live_flow estimates:
Dialogue1 336.30 s plan + 216.86 s readiness + 120 s load = 673.16 s;
Campsite 284.60 s plan + 210.20 s readiness + 120 s load = 614.80 s.
Both are below 780 s including the full readiness allowance. The dryrun's `-MaxMinutes 26` is the
aggregate cap for both launches (estimate 21.5 min), not a per-launch limit. Hold remains 90 s.
Config: hair_crowd's Ultra keys and startup latches, `visible_unfocused_fg`. Strands stay on screen.
Phases: startup baseline warmup and one combined candidate warmup, then PNG pairs base / shadow and
base / tips. Four PNG traces per scene, about 1,780 MB total including logs. Four cost windows remain
base / shadow / tips / base, with diagnostics off. The shared retimer sizes every 300-frame gate window
from the scene cadence (62.8 s in Dialogue1); shot windows retain their capture and float-energy gates.
The candidate warmup waits for the measured transmittance producer instead of a second cold-start
prefetch allowance. Producer sanity, all participation gates and fail-fast checks remain enabled.

Gates: hair_crowd's window gates without the crowd plan; strand G-buffer, velocity and groom participation in every
cost window; the `[hair primary] shadow transmittance:` line with measured draws and groom rows in cost-shadow.
After the run: character energy per shot pair (float pre-post mean, ROI as hair_primary); cost deltas against
mean(cost-base-a, cost-base-b); the user judges the shots.
