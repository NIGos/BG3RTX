## Default Ultra receiver probe deferral (branch rf-default, NOT QUALIFIED, 2026-11-06)
- Core applies rf_compact_probe_state / SpecId 1031 to every receiver-probe RF variant,
  including default Ultra rf_hit_shade=0,rf_hit_gi=0. Default remains 0; request delta 0.
- Construction audit: identical expressions/order; inputs unchanged, outputs unread,
  no caller return/discard between blocks, including the default rt_closest path.
- Key-off equivalence PASS 218/218 unchanged compiler inputs; default compile-only
  registers OFF/ON 128/96, shared bytes 0/1536, binary bytes 109312/110592.
- Numeric replay FAIL on corrected inn packs 093603-61 and 093623-62: 40 balanced
  adjacent phase 0/1 comparisons per OFF/OFF and OFF/ON run; 8 warmups/sample.
  Empirical nearest-rank control p99=0 in every channel; no tolerances or reruns.
- Pack 61: specular 63 differing pixels in 4/40 pairs; max RGB .0234375/.015625/.015625;
  mean signed RGB over 40 pairs 3.608e-10/4.963e-10/6.257e-10. R16 9 pixels,
  max 65503.992012, mean signed .0071942075 (sentinel differences included).
- Pack 62: specular 63 pixels in 40/40; max RGB .00604248/.00317383/.00244141;
  mean signed RGB -5.965e-11/-5.174e-11/-5.642e-11. R16 3 pixels in 7/40,
  max 65503.973465, mean signed -.00059951125. Diffuse exact, no nonfinite values.
- Mean-absolute/absolute-signed-mean gates fail too. Legacy whole-quartet controls
  vary outside the selected numeric samples; pack 62 raw counters vary in both runs.
- Paired ms median [95% CI]: candidate 61 -.122904 [-.157664,+.019344],
  control 61 -.000104 [-.125696,+.203392]; candidate 62 -.128760 [-.228944,-.016736],
  control 62 -.000432 [-.000976,+.015328]. Offline noise; no live saving claimed.
- Durable full-channel summary: docs/evidence/rf-default.json; raw replay logs/CSVs
  tools/replay_numeric.py reproduces the gate; tools/frame_replay.md documents it.
- Prepared default inn/S8 OFF/ON/OFF/ON, cost then energy, keep_reshade=false;
  both lints pass. Check reflection edges/cost and pre-post energy in these scenes
  only after further steering; not staged or run. Offline gate failed, key remains 0.
- Build/test: final build.ps1 -Test PASS 203/203 (174.65 s tests; 2 s build).
- No game, queue or Git writes. No junction cleanup. Raw outputs about 302 MB.
