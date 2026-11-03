# roof_shadow: whole-level roof selection off / on / off (STAGED)

Staged only: nothing is queued, launched or copied into a watched inbox. A live run needs the user's go for this
batch. `predicted.json` was written before the job. Design: `docs/TRACK_D_SUN.md` 12.5.

    py -3 -m tools.live_batches_staged.roof_shadow.make_job
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/roof_shadow
    py -3 tools/live_batches/live_flow.py --frozen tools/live_batches_staged/roof_shadow

| Job | Save | Windows per state (off-a / on / off-b) | After load (lint model) |
| --- | --- | --- | ---: |
| rs1-dialogue3-roof-world | Test Dialogue3 | static 60 + 1200 frames; energy (4 float traces, 5 s apart) | 256 s |

`visible_unfocused_fg`: the game never takes the focus. Ultra, FG 3x. The live key `rt_sun_roof_world` goes
0 / 1 / 0 inside one run at the static dialogue shot (the save loads in dialogue). `rt_shadows`, `rt_sun_replace`,
`rt_sun_roofs`, `rt_sun_roof_mode=0`, pad 3, up 50, radius 0 and `rt_cascade_casters=1` are set explicitly.

The static windows hold two `sun stats` periods (600 frames each, set by `core/sun_trust.cpp`, no stats reset): the
counters of a state's first line mix two states, so the report reads the last line of each window.

Runner gates: the cost lines in every window; every `sun stats` line of the ON window shows `roof query -1 m`,
of the OFF windows a positive radius; `over kSunMaxVolumes on 0 frames` everywhere; the enable line
`sun: world roof selection: level L, N volumes (N uploaded)` with total = uploaded.

`analyze.py` (the post command, `<log>.perf.md`): per window the roof volumes, px under roofs, kept by roofs, GPU ms,
and the energy of the ON state against the OFF pair (sane_gates floors). The band itself is judged on the PNGs of
the three energy windows: 4 per state, 5 s apart. It must stay still in ON and move in both OFF windows.
