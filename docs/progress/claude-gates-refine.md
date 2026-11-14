## Sensible gates refined, producer lint, handout decision recorded (branch claude/gates-refine, tools and docs, 2026-11-14)

- Cause: `sane_gates.compare` failed a difference beyond max(noise, floor). hq-inn-fix.1.211816 said FAIL on six
  lines that were no fault (RT +0.130 ms, 0.017 ms over the floor on 11.3 ms; GI +0.164 against noise 0.160; a p99 of
  +1.075 from 2 ON periods; timeouts 1 and 3 in the OFF hit windows; light rays +3% on an OFF pair 3% apart).
- Rule now (`tools/live_batches/sane_gates.py`, AGENTS.md): FAIL beyond noise plus floor, INFO between noise and
  noise plus floor. A p99 takes the full range of the OFF periods as noise. Events (`sane.events`): OFF windows are
  the baseline (INFO), ON fails only above the worst OFF window; counters without a baseline (fallbacks) still fail
  at 1. Equal counts (light rays at hits): OFF spread plus 1%, INFO inside it.
- Still failing (tests/sane_gates_test.py, 10 tests): mode 2 light rays 135707 against 3.84e6 (hq-s8-modes 205520),
  +1 ms frame time at 21 ms, the +4.4 ms hair-pan p99, a fallback, ON timeouts above OFF; energy -0.20% is EXPLAIN.
  Changed expectation: +0.5 ms at 21 ms with OFF periods 0.63 ms apart is now INFO (was FAIL).
- Re-read of the logs on disk (job files of today's main): hq-smoke-s8 PASS; hq-s8-high PASS; hq-inn-high PASS;
  hq-s8-modes.205520 PASS; hq-inn-fix EXPLAIN (RT and GI 1.5 ms lower in the rt_stats hit window; 0 failing, was 6);
  hq-s8-energy EXPLAIN (-0.20% energy, GI trace drop: the pre-fix defect); hq-inn-ultra EXPLAIN (7, the same pre-fix
  dropped hit work); hq-s8-ultra FAIL (hair-pan p99 +4.410 against noise 2.100 plus floor 0.260); hq-s8-modes.204217
  INVALID (stopped in the warm-up); perf-ab-inn, wide-split-0, wide-split-1 REVIEW_REQUIRED (tables only, no
  verdict rule); perf-ab-s8 INVALID (windows under 2 periods). The perf.md files in captures were not rewritten.
- Lint: `live_flow.PRODUCER_SETTINGS` knows `[core] GI far` needs `rt_stats=1` and `[core] scene handout` needs
  `rt_scene_handout=2` (the hq-s8-modes 204217 stop). The producer-sanity phase then switches them on itself. Frozen
  preflight unchanged: staged 23 jobs, 0 failing; live_batches 124 jobs, 71 failing, identical output before and after.
- Docs: SCENE_HANDOUT_DESIGN 8.8 and USER_SETTINGS record the user's decision (Low-latency scene On in High and
  Ultra, always with the hit wait fix) and the numbers (wait 3.5 to 0.3 ms S8, 8.5 to 2.9 ms inn; frame time
  unchanged on Ultra; about -0.5 ms on High at S8; 0 fallbacks; energy equal).
- Open: benefit in CPU-bound scenes unmeasured; the hq-s8-ultra hair-pan p99 FAIL never re-examined (its state
  columns may be shifted: energy OFF b scene wait reads 0.000 ms).
- No C++, shader or settings change; no build needed. variants.txt delta 0.
