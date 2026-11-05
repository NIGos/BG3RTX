## GI origin audit (branch origin-opt, rejected, 2026-11-05)

- Retained `frame_replay --origin` and `--compile-pass origin`; existing S8 pack has
  camera/depth/normal/geometry/alpha inputs. Full-resolution RGBA16F paired comparison.
- Trial `gi_origin_fast=0`, SpecId 1037: group-shared identical depth samples.
  S8 validated 40 pairs: ON-OFF +0.068368 ms, 95% CI [-0.093328,+0.333072] ms;
  every output and raw counter bit equal. No demonstrated saving: candidate removed.
- Driver OFF/ON: 80/80 registers; executable 59008/59776 bytes; shared 2304/2192 bytes.
  Stack 0; reported local memory 68719476752 (verbatim, not spill evidence).
  Achieved occupancy unavailable. Offline timings do not replace the live pass cost.
- Depth-derived interval already exists. Further narrowing lacks an exact nearest-hit,
  tie and candidate-cap proof. Alpha bypass / half-grid skip also lack equivalence.
- Original vs candidate-OFF outputs/counters bit-equal. Live-reference max difference
  6.103516e-05: reconstructed AS does not establish universal live equivalence.
- Shader/default/dispatch changes removed; 1037 retired; request delta 0, ABI unchanged.
  No game, queue or git writes. No in-game check needed for the removed trial.
- Evidence `.scratch/origin-opt/`: s8-valid, off-equivalence, stats-off/on and logs.
  Earlier binding-invalid runs corrected and repeated; not qualification evidence.
- Harness build and 2 CPU CTests pass; all 3 origin shader requests byte-identical to HEAD.
