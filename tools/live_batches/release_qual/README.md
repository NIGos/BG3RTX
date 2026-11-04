# Release qualification: prepared offline, no live authorization

Two independent launches, visible_unfocused_fg, FG 2. Jobs and plans stay here.
No shader requests added. No preset, renderer or launcher changes.

| Job | Scene | Expected minutes including 120 s load | Configuration |
| --- | --- | --- | --- |
| rel1-smoke-ultra-s8 | S8 / Water Queens House | 3.96 | Actual release Ultra, native stock hair/scalp, no strands |
| rel3-stock-hair-dialogue1 | Test Dialogue1 | 4.76 | Actual release Ultra; startup readiness, stock cards and visual shadow check |

Each launch has a 15-minute hard cap. Estimates use the current VALID corpus;
unknown readiness is unproven, not zero-cost evidence. Exact seconds, input
hashes, observables and pass rules are in each job's `predicted.json` directory.
Rebuild/regenerate after any settings/profile change and before live steering.

`profile.cpp` evaluates the same `code_defaults` and `apply_layer` functions as
the launcher: repository INI, release profile, then the selected preset. It
exports every effective raw key to isolate these jobs from stale user overrides.
Qualification overrides enable logging and one dialogue PNG. Ultra retains its
58 rendered fps cap, temporal post, native hair/scalp and automatic pool sizing.
Strand primary pass mask 15 is inert: all strand activation keys remain zero.

High keeps OMM/masks off; the High extension candidate was dropped from this batch.

Ultra keeps one static window, Q/E pans, both rotations and one ab_off
0 -> 1 -> 0 cycle. Shared live_flow sizes complete producer windows using the
measured S8 settle floor. Every measured window requires traced/composed finite
water, memory within the driver budget, RT capture/alpha overflow 0, cascade
buffer full 0 and scene timeouts 0. Current TLAS/RT GPU cost is required in the
full log. No OMM same-ray diagnostic or capture is scheduled: it was already
qualified twice. The master cycle restores RT.

Dialogue enables native coverage at startup, matching a player's first enable
and required restart. It waits for cache prefetch, scene-ready and a fresh quiet
pipeline watch; measured windows require zero pending/started/waiting builds.
The startup wait retains a 180 s cold-build allowance even when the fresh-watch
transition has no historical timing sample; the launch cap remains 15 minutes.
The occ diagnostic windows and expectation are removed after the 12:19 live
failed on a silent diagnostic producer during startup. The single static PNG
must be inspected for visible cards and physically correct face/neck shadows.
Logs cannot grant this visual acceptance.
Both jobs reject any hair primary/groom producer throughout the full log;
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
