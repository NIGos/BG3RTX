# perf_ab: where the user's configuration loses its frames (STAGED)

Five log-only launches, staged only: nothing is queued, launched or copied into a watched inbox. A live run
needs the user's go for this batch. `predicted.json` was written before the jobs.

    py -3 -m tools.live_batches_staged.perf_ab.make_jobs
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/perf_ab --preflight --tree .
    py -3 tools/live_batches/live_flow.py tools/live_batches_staged/perf_ab --preflight --frozen

The generator appends its new readiness queries to `tools/live_batches/lint_corpus_snapshot.json` (append only,
`lint_snapshot.extend_readiness`). Each job's post command writes `<log>.perf.md` with `analyze.py`: one table,
per window the median [min-max] of every cost line over the complete 120-frame periods, the `[mem]` guard
state and every `[vram] guard` line. Disk: logs only (no traces, bursts or dumps).

| Job | Save | Windows | After load (lint model) | Cap |
| --- | --- | --- | ---: | ---: |
| perf-ab-s8 | Water Queens House | 6 x (60 settle + 360 frames) | 135 s | 6 min |
| perf-ab-inn | The Blushing Mermaid - 96h 39m | the same 6 | 252 s at the model's 11.7 fps (about 125 s at 25 fps) | 6 min |
| perf-ab-wide-split-0, -1 | Water Queens House | Ultra static, High static (360 frames) | 47 s each | 5 min |
| perf-ab-dlss5-s8 (optional) | Water Queens House | 3 x (60 + 360 frames) | 74 s | 6 min |

Add about 100-120 s of loading per launch. Run order: s8, wide-split-1, wide-split-0, inn, dlss5.

## Jobs A and B: perf-ab-s8, perf-ab-inn

The user's configuration: `preset=ultra`, `fg=3`, RTX Hair On, `rt_hair_mv_card=2`, `rr_hair_strand_aa=2`,
`detailed_logging=1`, `cpu_timing=1`, static camera. Focused: no `window_mode`, `-Foreground`, so the game takes
the focus (no unfocused 30 fps cap; `[pace]` frame time and the FG ratio are valid). `fps_cap=0` for the whole
run: the Ultra preset and the user cap rendering at 58 fps, which would hide every cost below 17.2 ms.
`dev=1` (as the test tree's `bg3rtx.ini`): the release policy would otherwise undo a part toggled out of the
bundle. The bundle keys are read from `common/settings.h` at generation.

Each window: a fresh quiet `[watch]` period after the switch where a pipeline key changed, 60 frames in the
state, an acknowledged stats reset, then 360 frames (2-3 complete lines per timer).

| Window | State | Answers |
| --- | --- | --- |
| off-a | `rt_hair=0`, `rr_hair_strand_aa=0` (stock cards) | baseline |
| on | the RTX Hair On bundle, strand AA 2 | on - off: the whole RTX Hair cost, GPU lines against frame time |
| lod | + `rt_hair_pixel_lod=1`, small keep 0.25 | lod - on: distant detail 1/4 (camera BLAS and TLAS) |
| not | bundle, `rt_hair_groom_shadow_t=0`, `rt_hair_groom_ao_t=0` | on - not: transmittance in shadow and AO rays |
| groomrt | bundle, `rt_hair_groom_primary=0` | groomrt - off: the groom TLAS in secondary rays and groom refits (suspect 4); on - groomrt: the on-screen strand path (suspects 5 and 6) |
| off-b | as off-a | off-b - off-a: the noise floor (last column of the table) |

Requested window 5 (bundle with `rt_hair_groom=0`) is skipped: it is not a valid state. `layer/util.cpp`
("Groom gates primary strand swaps") and `common/hair_live.h` leave the grooms out of the build when the key
reads 0, so no strand is drawn and the window would repeat the baseline; the settings model also requires
`exp_hair_groom` for RTX Hair On. `groomrt` is the valid complement.

Live validity: the game starts with the whole bundle, `rt_hair_mv_card=2` and `rt_hair_groom_nearest=1`, so the
strand and velocity pipelines (specialization 1049) and the physical-origin reservation (`rt_hair_physical_origin`,
43.7 MB) exist from the level load; all five toggles are then live. Startup-only state that stays in every
window, the off windows included: the origin reservation, the 1049 velocity pipelines, `rt_capture_wide_split=1`.
The off windows are therefore "RTX Hair switched off live" (the hotkey's state), not "started without it".

Gates are presence only (`fail_fast` ends a run whose measured lines are missing): rt passes, GI, scene build,
scene wait, `[pace]`, `[watch]`, `[mem]`, RR, the hair period line and the FG state line in every window. Whether
strands were drawn is a row of the table (grooms published, G-buffer strand draws), so a scene without converted
grooms still delivers its other windows. The inn needs about 16 fps or more to finish under the 6 minute cap;
an aborted run keeps its completed windows (run `analyze.py` on its log by hand).

## Job C: perf-ab-wide-split-0 / -1

The checked-in `opt-profile-s8` job (the exact state of today's run 131416: release policy, FG 2x, unfocused)
without its pan windows; only `rt_capture_wide_split` differs. It is a startup key, so two launches: compare the
static log medians (differences under about 0.3 ms are run-to-run noise). Read `scene build` and its three parts,
`hold pack jobs`, `hold copy jobs` and bytes, `mesh pool` MB. Run 131416 (split 1) already shows, per 120 builds
in the static windows: Ultra hold pack 0 jobs and copy 29640 jobs (10.9 GB), High pack 1200 and copy 38764 (14.7 GB).
Unfocused, so frame time is the 30 fps cap there: GPU ms only.

## Job D: perf-ab-dlss5-s8 (optional)

Ultra, FG 3x, focused, stock cards; `dlss5_nr=1` at start, the live switch `dlss5_nr_bypass` 1 / 0 / 1
(`hotkey_dlss5` flips the same key). `dlss5_nr_runtime` is set to the user's path, but only the launcher reads
it: it copies the file to `sl/nvngx_dlssnr.dll` next to its own exe before Play. A queue job does not run the
launcher and this batch may not write the test tree, so the job is valid only if that file is already staged
there; otherwise it fails fast on `[dlss5] NR created` (no harm: RR output is kept). Read `DLSS 5 NR` and
`frame time median` in nr-on against the two off windows.
