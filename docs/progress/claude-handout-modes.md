## Scene handout modes job: warm-up stop was a job fault; hx1 control gate relaxed (2026-11-14)

- `hq-s8-modes.1.204217` stopped in phase 1 of 19 (`producer-sanity-warmup`, 144 s): "producer silent" on the GI far
  and `[core] scene handout` lines. Job fault, not code. Both lines were in the any-line `expect`, so live_flow asked
  for them right after the OFF warm-up. There `rt_stats=0` (GI far is an rt_stats line) and `rt_scene_handout=0`
  (core/gi.cpp `handout_log` returns before printing under key 2). No handout or fix state was ever reached.
- So the run says nothing about `rt_scene_handout_hit_wait`: 0 `[hit material submit]` lines (rt_stats off), 0 core
  handout lines. The fix is still unverified live (READY_FOR_LIVE).
- What the warm-up shows (OFF, static S8, 23 periods of 120 frames): scene wait mean 3.60 ms, 120 of 120 waited,
  0 timeouts; GI trace GPU median 4.65 ms; rt passes 6.6 ms; GI 8.7 to 8.9 ms; rendered median 21.5 ms; layout on,
  dummy TLAS recorded. These match the OFF states of hq-s8-energy (4.83 / 4.69 ms GI trace).
- Job fixed (`handout_qual/make_jobs.py` modes): the two lines left `expect`; the runner gates are "the line exists"
  per state: GI far and scene wait in every hit window, `[rt] scene handout` and `[core] scene handout` in hit-m2 and
  hit-fix. Plan is 18 phases, 339 s estimated, one launch, five states, 8 float traces each.
- Analyzer (`handout_qual/analyze.py`): the state table gained the rendered frame time; `state_costs` now judges GI
  trace GPU ms ("not higher"), scene wait mean and frame time ("lower") of every state against the OFF pair with
  sane_gates; the deliverable deltas are always printed. tests/sane_gates_test.py covers it (8 tests pass).
- hx1 (`hair_stable/make_job.py`): the keep-off control windows (in-nokeep, out-nokeep) only require their switches
  line. Run `hx1-s8-hair-stable.1.202905` (16 of 16 phases) was INVALID only because in-nokeep required `other 0` and
  saw `coverage stale 2, other 1`. Replayed with the new gates: every-window gates ok (26).
- Card runs of entering heads in 202905 (frames on cards, min / median / max): keep on: in-on 3 / 3 / 9, out-on
  3 / 3 / 3, orbit-r 3 / 6 / 6, orbit-l 3 / 3 / 3, in-on2 3 / 3 / 3, out-on2 3 / 3 / 3. Keep off: in-nokeep 1 / 9 / 10,
  out-nokeep 10 / 10 / 72. Predicted 3-4 against 10: held at the median. Two keep-on runs exceed the predicted cap of
  5 (9 in in-on, 6 in orbit-r). All six bundle windows: 0 strands->cards, 0 frames on cards, 0 watchdog trips.
- Lint live and frozen: 0 failing for both jobs. Snapshot: 0 new entries. No core, layer or shader change, no build.
- Left: live_flow's producer-sanity phase does not know that GI far needs rt_stats=1 or that the core handout line
  needs key 2 (`PRODUCER_SETTINGS`); lint passed the doomed job. Not changed here (shared tool, frozen lint).
