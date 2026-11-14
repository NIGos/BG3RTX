# handout_qual: qualify the scene handout in one live session (STAGED)

Staged only: nothing is queued, launched or copied into a watched inbox. A live run needs the user's go for this
batch. `predicted.json` was written before the jobs. Design and state: docs/SCENE_HANDOUT_DESIGN.md section 8.

    py -3 -m tools.live_batches_staged.handout_qual.make_jobs
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/handout_qual --preflight --tree .
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/handout_qual --preflight --frozen

Focused (`-Foreground`, no `window_mode`): the game takes the focus, so warn the user first. FG 3x. Every launch
starts with `rt_scene_handout_layout=1` (restart key) and toggles the live key `rt_scene_handout` 0 / 2 / 0 inside
the run. Each window: 60 frames in the state, an acknowledged stats reset, then the measured frames (120-frame
periods). Each job's post command writes `<log>.perf.md` with `analyze.py`.

| Job | Save | Windows (off-a / on / off-b each) | After load (lint model) |
| --- | --- | --- | ---: |
| hq-s8-ultra | Water Queens House | static 360; pan Q + E 240 each; RTX Hair pan Q + E; energy (3 float traces per state); primary-check pan Q + E | 616 s |
| hq-inn-ultra | The Blushing Mermaid - 96h 39m | the same without the primary-check windows | 513 s at 11.7 fps (about half at 25 fps) |
| hq-s8-high, hq-inn-high | both | static 360; pan Q + E 240 each | 154 s, 296 s |
| hq-smoke-s8 | Water Queens House | handout on from launch: static, pan Q, pan E, static | 77 s |
| hq-s8-energy | Water Queens House | 8 float traces per state, exposure held (decides the open S8 -0.72%) | 150 s |
| hq-s8-check | Water Queens House | primary-ray check pan Q + E (split out of hq-s8-ultra, 15 minute cap) | 576 s |
| hq-s8-modes | Water Queens House | rt_scene_handout 0 / 1 / 2 / 2 + rt_scene_handout_hit_wait / 0: 8 float traces and a short rt_stats window per state (design 8.6) | 317 s |

After the first run (2026-11-14): pans and statics are 3 periods per window (at least 2 complete periods wherever the
period grid falls); the per-pass GPU costs are judged "not higher beyond noise" (they fall when the wait goes).

Run order: hq-smoke-s8 (does the device start with the layout and hand out at all), hq-s8-ultra, hq-inn-ultra,
hq-s8-high, hq-inn-high. Add 100-120 s of loading per launch: about 35 minutes for the session.

## What is judged (analyze.py, thresholds from the run itself)

Verdicts follow `tools/live_batches/sane_gates.py` (user rule 2026-11-14): FAIL only beyond the OFF noise plus a
materiality floor in the bad direction (cost max(0.1 ms, 1%), energy 0.5%, match 1 point, equal counts 1%; a p99 takes
the full OFF range as noise; handout counters at 0, wait timeouts no more than the OFF windows have);
within noise plus floor INFO; an unpredicted cost drop or an energy difference under the floor is EXPLAIN, listed first;
a window with no complete period is not judged, and the run is INVALID only without a decisive window.

For each window group the ON median is compared with the OFF pair; noise = |off-b - off-a| of that group.

- Deliverable: `scene wait mean` and rendered `frame time median` lower (a gain is ON below the OFF mean by more than
  the noise); `frame time p99` not higher.
- Unchanged GPU work: rt passes, GI, scene build, strand G-buffer equal within noise.
- Counters, 0 in every ON window: wait timeouts, handout fallbacks, pairing mismatches, OMM downgrades,
  hit-material fallbacks (query, camera). The queue gates check the same per period (`expect_every`).
- The stale-scene detector: the handout is not a one-frame-old scene in order; only a fallback frame traces the
  previous scene. So the detectors are fallbacks = 0 in the pan windows and the primary-ray check match in motion
  (hq-s8-ultra `check-pan-*`): ON not lower than the OFF mean by more than the OFF a / b difference.
- Energy (AGENTS.md): 3 float traces per state on a static camera with the exposure held; the ON mean of the
  normalized scene colour must lie inside the range of the 6 OFF samples.
- RTX Hair: the hair windows switch the RTX Hair On bundle live (provisioned at launch as in perf_ab); grooms
  published and strand draws are rows of the table. A scene without converted grooms is inconclusive for hair.

Not covered: a scripted character walk. The plan runner has static, pan and rotate phases only; animated characters
are in view in both saves and are covered by the primary-ray match and the fallback counter. If the user wants the
walk, they turn the launcher option on and walk a character while watching the contact shadows at the feet.

## Smoke (hq-smoke-s8)

The state the launcher writes: release policy (`dev=0`), `preset=ultra` with its 58 fps cap, the knob's two keys,
RTX Hair On with distant detail 1/4 and motion stability, every other qualified key from the preset. Gates: handed
out with 0 fallbacks, 0 wait timeouts, no opaque draw dropped, FG presented 3 per frame, no layout refusal.

## Decision

Qualified when every counter and equality gate passes in all five launches. On by default only if the rendered
median gains more than the noise in at least one cost launch and loses in none (sh2 at S8 on 2026-11-09 gained
0.4-0.6 ms: GPU-bound there).
