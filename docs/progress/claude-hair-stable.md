## RTX Hair stability: GPU header camera in the bundle, per-pixel card fill, refit disproved (branch claude/hair-stable, READY_FOR_LIVE, 2026-11-14)

Design and history: docs/RTX_HAIR_PRIMARY.md 6g. Offline only; no live run.

- What the user saw (play log of 0.9.6-hairtest13, about 2 minutes of hair, Ultra): 2927 hair frames, 960 (33%) with
  every hair draw on cards; 139 switches to cards (frame watchdog 72, coverage low 35, other 26, own toggle 6);
  161 draws under a tenth of their card pixels (the holes). Still camera: 0 switches.
- Cause of nearly all of it: `rt_hair_header_cam_gpu` was 0. Every trip had its ray origin one camera step off
  (0.08-0.57 m; sampled 0.308 and 0.570 m). The losses were real (252 draws under their bound, 8 above), not gate noise.
- Kept: RTX Hair On now writes `rt_hair_header_cam_gpu=1` (old files get it on load). Proven live before
  (hc1 234025 strand share 0.93-0.98, hl1 zoom 2-7% -> 50-70%); cost a 16-thread dispatch and two 12-byte copies.
- New, off: `rt_hair_card_fill` (Advanced knob, restart to turn on; specialization 1053 + header flag 512). A hair
  pixel without a strand draws the game's card instead of a hole (behind a strand another fragment writes). With it
  the frame watchdog only reports and a swapping key returns to cards after 28 low measurements in a row, not 2.
  Visible change when still: the 3-7% of card pixels without a strand show the card.
- Proof offline: key off equals origin/main after specialization (canonical SPIR-V, 16 combinations); raw identity
  225 of 227 (the two are rt/hair_primary_gbuffer.hlsl, spirv-val passed); shaders/variants.txt delta 0.
  CTest test_card_fill; settings_test and schema updated.
- Disproved: the LSS refit as the cause of the dolly loss. In-place refit of a compacted BLAS under 0.5-1.3 m per
  frame keeps 61347 of 61347 hits and differs from a fresh build in at most 1 of 65536 pixels (RTX 5090, CTest
  hair_primary_gpu_test refit_check).
- Open: the 30-50% strand loss under a fast dolly with the GPU camera (6d-6f) is NOT solved. It cannot be proven
  from the code or the logs on disk (hl1 logs pruned). Six named candidates with the dense-sample signature of each
  are in `tools/live_batches_staged/hair_stable/predicted.json`. Strands in every moving frame depend on that live.
- Not changed: styles without a converted groom stay cards (3399, 7086, 4077, 1623, 9384 indices in the play log).
- Staged, not queued: `tools/live_batches_staged/hair_stable/` job hx1-s8-hair-stable (S8, one launch, about 65 s of
  phases): wheel dolly in and out and orbit with the fill on, the same dolly with the fill off, dense samples.
  Gate: every switches line in the fill windows has "frame watchdog 0, key watchdog 0, coverage low 0".
  Lint frozen 0 failing (snapshot appended only); merge_scope tooling exit 0.
- User check after a go: S8, RTX Hair On, "Strand hair: card under missing strands" On, restart; zoom in fast with
  the wheel and pan: no hole, no head popping to cards.
- Next: if the gate holds, add `rt_hair_card_fill` to the On bundle; fix the cause the dense samples name.
