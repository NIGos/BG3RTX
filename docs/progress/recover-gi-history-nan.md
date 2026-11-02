## GI history NaN/Inf safety recovered from the codex research stack (branch recover-gi-history-nan, offline verified, 2026-11-02)

- From codex 06f62f9 (gi_temporal guard) and 9078be6 (NGX MV scale), see docs/CODEX_BRANCH_TRIAGE.md.
- shaders/gi/gi_temporal.hlsl: rejected history now writes the fresh sample by select, not
  `lerp(hist, cur, 1)` (= hist * 0 + cur, NaN for a NaN/Inf history texel, which then never recovered);
  a non-finite reprojected position is rejected before the float-to-int conversion.
- layer/ngx.cpp: MV_Scale_X/Y are stored only when both are read and finite (else the last valid scale).
- Pure fix, no key: the same values for finite inputs. Only gi/gi_temporal.hlsl (and its variants)
  changes SPIR-V; the layer part needs a game restart.
- Not recovered (needs a variant key and a live pan A/B): motion read at the depth sample pixel in
  gi_temporal, and rejecting off-screen previous-radiance reprojection (triage items 2 and 3).
