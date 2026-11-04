# Release qualification: prepared offline, no live authorization

Three independent launches, visible_unfocused_fg, FG 2. Jobs and plans stay here.
No shader requests added. No preset, renderer or launcher changes.

| Job | Scene | Expected minutes including 120 s load | Configuration |
| --- | --- | --- | --- |
| rel1-smoke-ultra-s8 | S8 / Water Queens House | 7.0 | Actual release Ultra, native stock hair/scalp, no strands |
| rel2-smoke-high-s8 | S8 / Water Queens House | 7.0 | Release High rendering plus explicit OMM/mask extension candidate |
| rel3-stock-hair-dialogue1 | Test Dialogue1 | 5.4 | Actual release Ultra; startup readiness, stock cards and character shadows |

Each launch has a 15-minute hard cap. Estimates use the current VALID corpus;
unknown readiness is unproven, not zero-cost evidence. Exact seconds, input
hashes, observables and pass rules are in each job's `predicted.json` directory.
Rebuild/regenerate after any settings/profile change and before live steering.

`profile.cpp` evaluates the same `code_defaults` and `apply_layer` functions as
the launcher: repository INI, release profile, then the selected preset. It
exports every effective raw key to isolate these jobs from stale user overrides.
Qualification overrides enable only logging and PNG capture. Ultra retains its
58 rendered fps cap, temporal post, native hair/scalp and automatic pool sizing.
Strand primary pass mask 15 is inert: all strand activation keys remain zero.

High caveat: CONTROL_CONTRACT currently vetoes OMM/masks in dev=0 High. Rel2 uses
dev=1 with exactly four rendering changes: rt_omm, rt_omm_dyn,
rt_alpha_fine_lod and rt_vt_alpha_masks = 1. All other raw settings are exported
from release High. In particular water, bounce, stock-hair capture and box
shadows stay Off, as High requires. This is the extension candidate gate, not a
claim that the current launcher ships High with that bundle. A passing candidate
needs stream A's policy promotion and a final dev=0 High confirmation before
shipping the extension. Do not enable Ultra's water just to satisfy a High gate.

Smoke phases/gates come from smoke_ultra; dialogue phase construction uses
hair_qual's shared `step` helper without its retired physical-origin experiments.
Shared live_flow adds readiness and sizes complete producer windows. Every
measured smoke window requires current TLAS participation, RT/GI GPU ms,
driver-budget VRAM, native mask/fine-LOD and dynamic OMM participation. Capacity,
scene-timeout and same-ray violation gates include the diagnostic window.
Ultra additionally gates water and cascade participation. Diagnostics stay out
of cost windows. One ab_off 0 -> 1 -> 0 cycle exercises the same master path as
Ctrl+Shift+F9 and restores RT. No keyboard shortcut is injected separately.

Dialogue enables native coverage at startup, matching a player's first enable
and required restart. It waits for cache prefetch, scene-ready and a fresh quiet
pipeline watch; measured windows require zero pending/started/waiting builds.
The startup wait retains a 180 s cold-build allowance even when the fresh-watch
transition has no historical timing sample; the launch cap remains 15 minutes.
A separate occ window requires nonzero character sun rays and sun/AO + local
pass participation. Existing counters classify character pixels, not anatomical
face/neck regions: the two static PNGs must be inspected for visible cards and
physically correct face/neck shadows. Logs cannot grant this visual acceptance.
All three jobs reject any hair primary/groom producer throughout the full log;
absence passes, so inactive strands cannot satisfy an activation gate.

Regenerate from the repository root (compiler output belongs in TEMP):

```powershell
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 tools/live_batches/release_qual/profile.cpp -o "$env:TEMP/bg3rtx-release-profile.exe"
python tools/live_batches/release_qual/make_jobs.py --profile-exe "$env:TEMP/bg3rtx-release-profile.exe"
python tools/live_batches/live_flow.py tools/live_batches/release_qual --preflight --cache tools/live_batches/release_qual/offline/corpus.json --report tools/live_batches/release_qual/offline/live_flow.json
python tests/batch_jobs_lint_test.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/release_qual/dryrun.ps1 -Force
```

The dryrun wrapper selects each job separately in lean_dryrun, including camera,
role, scene-wait/capacity, budget and fake-queue negative/positive checks. Its
synthetic inboxes exist only in TEMP. It never accesses the real pending inbox
or launches a game. Receipts are offline preparation, not quality passes.
