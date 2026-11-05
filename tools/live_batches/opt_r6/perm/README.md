# GI permutation and scalar AO GPU receipt

READY_FOR_LIVE. Offline preparation only; no queue staging or launch.
Copied from opt_r5/compact_hit: inn and S8 Ultra, OFF/ON/OFF/ON,
600 rendered frames per window, reset-aligned 120-frame GPU timers.
Both gi_lane_permute and gi_ao_r16f toggle together; dev=1 in every window.
Warm ON first, fresh QUIET readiness after every toggle. Both formats stay
resident after warmup so toggles preserve temporal histories. No dumps.

GI trace decides SAVING only if both ON medians beat both OFF medians by more
than the OFF repeat spread. Other pass medians are reported, including AO in
GI total; a batch saving does not attribute a separate win to scalar AO.
Missing readiness/timer evidence is INVALID. Five-minute launch cap retained.
No live saving claimed. See docs/progress/gi-perm.md for offline evidence.

Generate: python tools/live_batches/opt_r6/perm/make_job.py --profile-exe .scratch/perm-profile.exe
Lint with tools/live_batches/live_flow.py tools/live_batches/opt_r6/perm,
using --frozen for the frozen snapshot and --results for the current VALID corpus.
Explicit user authorization is required before staging or launching.

Tooling prerequisite (owned by the other lane): register gi_lane_permute with
GI_TRACE and gi_ao_r16f with the gi_ao_apply/rt_local_shadows pipeline family in
pipeline_readiness.TABLE. Current frozen and non-frozen reports contain only
these missing-contract findings. Mark READY_FOR_LIVE only after dependent
offline checks and both lint modes pass. Existing replay timings include stats.

The exact additive registry change is in `readiness-integration.patch`; the owning
lane/coordinator applies it. Integration lint receipts simulate only these two lines.
