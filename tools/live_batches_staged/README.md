# Staged live batches

Jobs prepared outside every watched inbox (AGENTS.md, "Autonomous rendering program"). Nothing here is queued.
A job goes to `captures/queue/pending` only after the user's go for that batch, and after
`py -3 tools/live_batches/live_flow.py --frozen tools/live_batches_staged/<batch>` reports 0 failing.
Regenerate a job from its `make_job.py` / `make_jobs.py` before use: the committed JSON is the last generated state.

| Batch | Job | State (2026-11-14) | Use |
|---|---|---|---|
| `hair_stable` | hx1-s8-hair-stable | Current. Ran 190905, 194658, 202905. | The RTX Hair On bundle as the launcher writes it; rerun after any strand change. |
| `perf_ab` | 5 jobs | Staged, not run. | Performance A/B of the release profile with `sane_gates.py`. |
| `handout_qual` | 9 jobs | Done (scene handout qualified, SCENE_HANDOUT_DESIGN 8.5 to 8.8). | Template for an Off / On / Off qualification with `sane_gates.py`; `tests/sane_gates_test.py` imports its `analyze.py`. |
| `hair_pixel_lod` | pl1-s8-small-heads | Not current: sets the removed `rt_hair_row_stable` and predates the 6k bundle. | Template for the "RTX Hair detail at distance" qualification; regenerate from the bundle first. |
| `hair_mv` | hm1-s8-hair-mv | Done (motion stability is On in High and Ultra). | Template for a motion-vector check with float bursts. |
| `hair_static` | hs1-s8-static-hair | Done (5p). | Template for a static-camera flicker burst. |
| `hair_fg` | hf1-s8-hair-fg | Done (5s). | Template for presented-frame (Frame Generation) captures. |

Removed on 2026-11-14 (they set keys that no longer exist and were superseded by `hair_stable`; their runs are
described in docs/RTX_HAIR_PRIMARY.md 6c to 6k, and the files are in git history before this commit):
`hair_cam` (hc1, hk1), `hair_late` (hl1), `hair_final` (hf2).
