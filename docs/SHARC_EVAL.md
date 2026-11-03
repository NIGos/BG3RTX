# SHaRC offline evaluation (2026-11-03)
Verdict: **KEEP EXPERIMENTAL, OFF (`sharc=0`); no promotion candidate.**
The goal's “only if it beats the PT reference” gate is NOT demonstrated.
This is an evidence limitation, not proof that cache removal improves quality.
No game launch, queue write, git metadata change, commit, or production edit.
Requested branch: sharc-eval; supplied checkout: pak-textures at 8120c02a.
Existing core/gi.cpp changes were left untouched; no branch switch under read-only git.
## What can actually be compared
Packs are under `.claude/worktrees/test-main/captures/` in the main checkout.
S8: replay-20261101-142819-1; camp: replay-20261102-013545-2;
inn: replay-20261102-013914-3; S6: replay-20261102-013032-1 and replay-20261102-035919-1.
All five contain one base gi_hit_direct frame, zero set 4 cache directives, no sequence.
common/replay_pack.h binds sets 0–3; core/gi.cpp replay_after_hit_gi rejects SHaRC.
frame_replay --repeat reuses the same push/input, without injection, resolve or history.
SHaRC needs set 4 state and post-composite diffuse injection from preceding frames.
An empty cache or repeated frozen frame cannot stand in for the production cache.
Existing cache-only, hit-direct, sun, emission, lock-injection and fine-grid variants
do not supply these missing inputs. None has a valid ON/OFF replay/PT comparison.

## Numbers available (not SHaRC qualification)
Recomputed from retained PFMs: native E × game AO vs k × PT far + PT environment,
Rec.709 luminance, 2,731,520 valid pixels per scene; bias = native minus reference.
| Scene | Native / reference mean | Bias | RMSE | Energy ratio |
|---|---|---|---|---|
| Camp | 0.646319 / 0.765843 | -0.119523 (-15.61%) | 0.337235 | 0.843932 |
| Inn | 0.113955 / 0.146005 | -0.032050 (-21.95%) | 0.100429 | 0.780487 |
S8 retained baseline: ratio 0.655730, RMSE 0.285221, **invalid for qualification: no roofs**.
The corrected historical roof-model ratio is 1.44 (TRACK_C_GI); do not mix the models.
S6 has no retained PT output. SHaRC OFF/ON bias, RMSE, energy and their deltas: unavailable.
Interior/foliage semantic masks and ON images are absent; regional errors: unavailable.
Temporal variance: unavailable (one frame per scene; different scenes are not time samples).
Historical sh1 S8: cache found 23.8%; occluded-cell hits 10.13% (rejection enabled).
Re-read cost windows: trace 4.309/4.810/4.323 ms OFF/ON/OFF (+0.494 ms);
reflections +0.168 ms; resolve/inject 0.032/0.068 ms; whole GI +0.810 ms.
Historical progress records energy -0.86% vs predicted +1.5%; raw dumps are now missing.
Re-running m4_promotion_gate returns INCOMPLETE, so that energy cannot be re-qualified.
PT self-test: 11/11 passed, zero validation errors. Evidence and SHA-256 inputs:
`captures/sharc-eval/{evaluate.py,baseline.json,pt-self-test.log,sh1-gate.txt}` in this checkout.
No removal or shader edits: key-off identity unchanged by construction; variant delta 0.
Revisit only with cache state/history plus matched PT inputs and region masks; no live job prepared.
