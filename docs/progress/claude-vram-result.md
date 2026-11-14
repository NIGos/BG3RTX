## VRAM right-sizing live result: 4 of 5 policies qualify, -0.8 to -1.0 GB, no fault (branch claude/vram-result, READY to default, 2026-11-14)

Runs `vr-s8.1.232616` and `vr-inn.1.233221` (Ultra, RTX Hair On, FG 3x, visible unfocused, 22 of 22 phases, 0 occluded
checks). INVALID only for the post gate: the job's `post` starts with `python`, which cmd resolved to the Store alias
("Python was not found"), so `analyze.py` never ran. Fix: the runner sends a leading `python` through `py -3` (34 job
files use it; dryrun job `l2-post-python`). Run by hand, the analyzer reports 0 missing observations on both logs.
Reserved MB (used MB in the first column); OFF = the off-a and off-b states:

| Pool | S8 OFF | S8 ON | Inn OFF | Inn ON | Saving S8 / inn |
| --- | ---: | ---: | ---: | ---: | --- |
| Cascade capture, 3 slots (535-816) | 1136-1408 | 1088 | 1424-1584 | 1152 | -50 to -320 / -270 to -430 |
| RT capture (257-565) | 1408 | 736 | 1776-2047 | 1184, dolly peak 1776, then 1408 | -672 / -590 to -860, 0 to -270 at the peak |
| Mesh pool (520-1270) | 1024, 1455 | 727, 1017 | 2047 | 2047 | 2 trims, -297 and -438 / 0 (deferred) |
| BLAS chunks, dolly mean (450-810) | 1042-1347 | 1356 | 2354-2774 | 2694 | none (empty chunks 0 / -270) |
| Hit-material, OMM, hair | 168, 5, 820 | same | 0-300, 5, 800 | same | 0 |
| VRAM used, all windows (budget 31419) | 16422-16456 | 15648 | 18587-18895 | 17697 | -790 / -1040 (tracked -710 / -800) |

- Against `predicted.json`: static 0.3-0.9 GB predicted, 0.8 and 1.0 measured; moving 0.7-2.0 predicted, 0.74 (S8) and
  0.47 (inn, inside the OFF pair's 0.58) measured. Disproved: `rt_as_chunk_fit` (empty chunks 0-200 MB predicted, up
  to 911 MB seen; 22 chunks created in the inn dolly-out with the key, 22 and 23 without).
- Faults in the plan, both runs, every state: draws dropped (buffer full) 0.00, cascade buffer full 0.0, stride split
  left out 0.00, guard normal, 0 refusals, 0 scene wait timeouts, 0 handout fallbacks, 0 strands to cards, no `VK_ERROR`.
- Demand past the reserve: only the inn dolly-out with the keys on (opaque 126 -> 558 MB): two growths (1184 -> 1424
  -> 1776 MB), one deferred on the fence, 0 draws left out. S8: no growth in any ON window.
- Hitches (per-frame `[stats]` intervals, 33 ms cap): trims 38 and 48 ms, RT shrinks 45-51 ms, cascade 36-56 ms. Inn
  dolly-out max 234 / 248 / 209 ms, 851 / 1079 / 1130 ms over 50 ms (off-a / on / off-b): the dolly's own BLAS
  builds, also without a replacement. Pans and statics: max 38-48 ms in every state.
- GPU ms equal: S8 static RT 10.98 on, 11.38 off-b; scene build 2.93, 2.91. Inn RT 12.93, 12.93; build 2.77, 2.71.
- Energy (4 float traces per state, exposure held): S8 on 833.14 in OFF 830.36-838.72 (-0.25%); inn on 151.10 in OFF
  148.77-155.28 (-0.18%); under the 0.5% floor. Confound: the dolly-in ends closer than the start (S8 12 -> 13 grooms,
  inn 11 -> 8), so for statics and pans ON is compared with off-b.
- Verdict: `rt_capture_demand_size` on (-0.6 to -0.9 GB). `rt_capture_peak_reserve` on (-0.05 to -0.43 GB).
  `rt_capture_split_mb` on with them (toggled together, no fault; its smaller first buffers at launch were not run:
  confirm in the next authorized live with the keys on from the start). `rt_mesh_pool_trim` on (no hitch); no effect
  when a groom sits at the pool's end (inn: 2047 MB for 690 used; needs groom ranges that move). `rt_as_chunk_fit`
  stays 0: no saving, no harm. The branch's last commit turns the four on (drop it to keep them off). Cost: 0 GPU ms.
- Left: after a dolly the inn's BLAS chunks hold 2.3-2.7 GB for 0.65-0.8 GB live in every state (partly filled
  chunks, back to 0.9-1.0 GB after about 15 s still): the largest reserve in motion, not touched by these keys.
- User check: `detailed_logging=1`, play, read `[mem] pools reserved/used MB` (`policies ...=1`; RT capture 700-1200 MB at rest,
  not 1408-2047); Task Manager, dedicated GPU memory of bg3.exe 0.8-1 GB lower in the same place.
