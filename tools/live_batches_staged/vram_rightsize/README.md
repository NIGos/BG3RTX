# vram_rightsize: the VRAM right-sizing policies off / on / off (STAGED)

Staged only: nothing is queued, launched or copied into a watched inbox. A live run needs the user's go for this
batch. `predicted.json` was written before the jobs. Findings and replays: `docs/progress/claude-vram-rightsize.md`.

    py -3 -m tools.live_batches_staged.vram_rightsize.make_jobs
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/vram_rightsize --preflight --tree .
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/vram_rightsize --preflight --frozen

`visible_unfocused_fg`: the game never takes the focus. Ultra, RTX Hair On, FG 3x. The five live keys
`rt_capture_peak_reserve`, `rt_capture_demand_size`, `rt_capture_split_mb`, `rt_mesh_pool_trim`, `rt_as_chunk_fit`
go 0 / 1 / 0 together inside one run. Each window: 60 frames in the state, an acknowledged stats reset, then the
measured frames. The post command writes `<log>.perf.md` with `analyze.py`.

| Job | Save | Windows per state (off-a / on / off-b) | After load (lint model) |
| --- | --- | --- | ---: |
| vr-s8 | Water Queens House | static 360; pan Q + E 240 each; wheel dolly out + in 240 each; energy (4 float traces) | 293 s |
| vr-inn | The Blushing Mermaid - 96h 39m | the same | 514 s at the model's 11.7 fps |

The static and dolly windows are longer than one period on purpose (the lint's duration notes): a capture shrinks
after 120 low samples and the pool trim needs a quiet window of 254 frames.

Runner gates are presence only: the cost lines and the `[mem] pools reserved/used MB` line in every window, and the
`policies ...=1` / `=0` state of that line in the ON / OFF windows. `analyze.py` reports per window the reserved and
used MB of every pool, VRAM used and budget, the replacement, refusal and left-out counts, GPU ms, frame time max
and p99, scene wait max, and the energy of the ON state against the OFF pair. Frame time and FG presentation are
not judged from an occluded window.
