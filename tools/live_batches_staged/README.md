# Staged live batches

Jobs prepared outside every watched inbox (AGENTS.md, "Autonomous rendering program"). Nothing here is queued.
A job goes to `captures/queue/pending` only after the user's go for that batch, and after
`py -3 tools/live_batches/live_flow.py --frozen tools/live_batches_staged/<batch>` reports 0 failing.
Regenerate a job from its `make_job.py` / `make_jobs.py` before use: the committed JSON is the last generated state.

| Batch | Job | State (2026-11-20) | Use |
|---|---|---|---|
| `release_final` | final-s8-ultra-high, final-stock-hair-dialogue1 (in `final/jobs`) | Staged 2026-11-15, NOT queued: needs a user go. Never run. | The final release smoke for 0.9.6: the 2026-11-04 release_qual receipts regenerated from today's release profile (Ultra then High at S8, Ultra in Dialogue1), plus handout and capture-drop gates. Queue only the two files in `final/jobs`; `jobs/` holds their three sources. `predicted.json`, `schedule.json` (3.8 and 3.35 minutes, 130 and 70 MB). |
| `hair_stable` | hx1-s8-hair-stable | Current. Ran 190905, 194658, 202905. | The RTX Hair On bundle as the launcher writes it; rerun after any strand change. |
| `perf_ab` | 5 jobs | Staged, not run. perf-ab-wide-split-0/-1 are obsolete in purpose (`rt_capture_wide_split` is on by default) and carry two removed keys from their opt-profile-s8 source (`gi_roof_reject`, `rt_origin_lazy_normal`: no code reads them, harmless). | Performance A/B of the release profile with `sane_gates.py`. Its `make_jobs.py` holds the shared generator helpers (`phase`, `energy_phase`, `every`, `focused_job`, `retime`, `write_made`). |
| `final_layer` | fl-s8-holdpack, fl-header-early | Staged 2026-11-20, READY_FOR_LIVE, not queued. | `rt_hold_pack_base` 0 / 1 / 0 at S8; two launches with `rt_hair_header_early=1`. |
| `r4_hitch` | r4-inn | Staged 2026-11-21, READY_FOR_LIVE, not queued. | `rt_hair_groom_async` 0 / 1 / 0 at the inn with r2-inn's dollies: frame time p99 and scene wait max of the outward dollies against the OFF windows (the decode line says `, worker` in ON). |
| `r2_tools` | r2-inn-roof-em | Staged 2026-11-20, READY_FOR_LIVE, not queued. | `gi_far_roof_emission` 0 / 1 / 0 at the inn, plus one GI replay pack for the offline PT reference (`predicted.json` "offline"). |
| `dlss5` | dlss5-s8-ultra-native | Staged 2026-11-05, committed 2026-11-20, not queued. | Native DLSS 5 NR OFF / ON / OFF / ON at S8 with a PNG pair; needs the user's runtime staged by the launcher (its README). |
| `vram_rightsize` | vr-s8, vr-inn | Done (ran 2026-11-14 night; the four VRAM policies are on by default). | Template for pan and dolly windows with pool rows (`analyze.py`). |
| `handout_qual` | 9 jobs | Done (scene handout qualified, SCENE_HANDOUT_DESIGN 8.5 to 8.8). | Template for an Off / On / Off qualification with `sane_gates.py`; `tests/sane_gates_test.py` imports its `analyze.py`. |
| `hair_mv` | hm1-s8-hair-mv | Done (motion stability is On in High and Ultra). | Template for a motion-vector check with float bursts. |
| `hair_static` | hs1-s8-static-hair | Done (5p). | Template for a static-camera flicker burst. |
| `hair_fg` | hf1-s8-hair-fg | Done (5s). | Template for presented-frame (Frame Generation) captures. |

Removed on 2026-11-14 (they set keys that no longer exist and were superseded by `hair_stable`; their runs are
described in docs/RTX_HAIR_PRIMARY.md 6c to 6k, and the files are in git history before this commit):
`hair_cam` (hc1, hk1), `hair_late` (hl1), `hair_final` (hf2). Removed on 2026-11-20 for the same reason:
`hair_pixel_lod` (pl1, it set the removed `rt_hair_row_stable`; rebuild from `hair_stable` for a detail-at-distance live).
