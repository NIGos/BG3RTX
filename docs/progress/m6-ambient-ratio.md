## M6 step 1: native ambient against the PT reference on 3 scenes (main, measured, 2026-11-02)

- Packs captured live (rc2 camp 01:34, rc3 inn 01:37, both valid), replayed offline with the casters in the TLAS
  (post_replay.ps1 -Scene). Kept: replay-20261102-013545-2 (camp), replay-20261102-013914-3 (inn), S8 142819-1.
- E_native x AO / (k x PT far + PT env), tiles p10 / median / p90:
  - S8 (Water Queens House): 1.44, 0.90 / 1.75 / 3.01 (earlier pack, docs/PT_FEASIBILITY_OFFLINE.md)
  - camp (outdoor, sun 98%): 0.84, 0.63 / 0.81 / 1.15; far field 74% of the reference, k 0.75
  - inn (interior, probes 99.9% of froxels): 0.78, 0.53 / 0.77 / 1.02; far field 82%, k 0.25
- Verdict: not consistent across scenes (S8 native brighter, camp and inn darker by about 20%). No systematic
  ambient correction (goal rule: only if coherent on every scene). Camp and inn agree within their tile spread;
  S8 is the outlier, so the next question is what S8 has that the others lack, not a global scale.
- Next in M6: the gi_far gap (-0.3% live against -2% predicted), then SHaRC.
