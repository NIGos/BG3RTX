# wf9 qualification run, 2026-11-04 08:59 (VALID)

Main 6a00b295, real Ultra, S8 Water Queens House, visible_unfocused_fg, 441 s. Key rt_water_compose_shift
0/1/0/1 in one session. All 17 every-window gates pass. Compose TLAS age avg/max 1.00/1 frame, camera missing 0.
The first attempt (08:52) was INVALID: cold runtime shader cache after bundle25, 44 compiles, prefetch timeout.

| window | key | compose screen hit | compose hidden | water_reflect screen hit |
|---|---|---|---|---|
| static 0-3 | 0/1/0/1 | 14.9-15.0% | 6.4-6.5% | 14.8-14.9% |
| cost-turn-1 (E) | 1 | 16.4-17.2% | 15.3-15.7% | 18.8-19.6% |
| cost-turn-2 (E) | 0 | 6.6-7.2% | 24.7-26.2% | 18.8-19.7% |
| cost-turn-3 (Q) | 1 | 16.5-17.3% | 15.3-15.7% | 18.8-19.6% |

- Static: ON equals OFF (no change without motion), as predicted.
- Turning: ON recovers compose screen hits from 6.7% to about 17% (reference water_reflect 19.6% in the same
  window) and hidden from 26% to 15.5% (predicted about 13%). Residual gap about 2.5 points of screen hits.
- Compose GPU 0.154 ms median at the end of the run.

Decision: qualified. The key is already in Ultra; per the release plan it becomes always on with water.
