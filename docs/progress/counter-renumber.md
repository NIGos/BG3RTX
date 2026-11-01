## rtStats renumber: rf_hit_gi + rt_hit_material_wind_rgb in one tree (branch counter-renumber, offline verified, 2026-11-01)

- Both branches used rtStats 419..426. Merged from main: worktree-agent-a05080ba75170ea35 (rf_hit_gi, keeps
  419..426) then worktree-agent-a87dc09be59810f7c (wind RGB 2S clip check, the later commit: now 427..434).
  kStatsCounters 435 (<= 448, frame_replay's HM_BLOCKERS slots; <= 512). Updated: core/gi.cpp kHitClip3First +
  static_assert, native_hit_material.hlsli base 427, the counter map comment, frame_replay's comment,
  TRACK_D_REFLECTIONS, the wind RGB progress note. No log text or gate regex names the indices.
- Conflicts: main's shader tooling rewrite vs rf_hit_gi's test edits. tools/shader_identity.feature_gate now takes
  a feature predicate and `foreign` (another gate's new requests, here RF_HIT_GI=1); rf_hit_shade's gate owns the
  RF_HIT_GI requests (contract --gi, --em by file); compile_shaders passes --gi. m4p3/m4p4 jobs: rf_hit_gi's plan
  plus main's scene-wait gate (windows recomputed, incl. its new cost windows); m4w job got the scene-wait gate.
- build -Test: 89/89 CTests pass. Shader byte identity vs main: see the next line (tools/shader_identity.py).
