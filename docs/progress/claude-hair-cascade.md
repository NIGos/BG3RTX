## Hair cascade consistency (branch claude-hair-cascade, NEEDS_IMPLEMENTATION, 2026-11-04)
- Ultra uses RT sun shadows and cascade casters (`common/settings.h:173`); current-native mode stays off (`:645`).
- Skin/neck/shoulders use the same receiver trust as other materials; skin transmission is traced too
  (`shaders/gi/rt_shadow.hlsl:87`). Groom-live status does not override trust.
- At strength 1, trusted traced pixels use strand visibility; roof pixels retain native mask * RT visibility,
  off-screen trust blends that fold with RT, and unknown rays retain the native mask exactly
  (`rt_shadow.hlsl:195`, `rt_sun_resolve.hlsl:38`, `core/sun_trust.cpp:178`).
- Missing TLAS/trust/dispatch/resolve also prevents full replacement (`core/gi.cpp:2961`, `:3111`).
- Cascade capture adds lagged off-camera casters (`layer/rtscene.cpp:1005`); it does not remove native draws.
- Camera cards get kInfoGroomReplaced (`layer/bvhsplit.cpp:2885`); native cascade records lack that identity
  (`layer/host.h:615`). Index-range keys and live membership are separate (`layer/hair_primary.cpp:527`, `:955`).
- A rejected: removing a shared cascade draw also removes its shadow on fallback receivers. Draw-time
  groom membership cannot certify later per-pixel roof, off-screen, unknown-ray or resource fallback.
- B not implemented: the native R8 mask has no separate hair term. Replacing it wholesale would also discard
  unknown non-hair occluders; the native-domain path needs an epoch-matched cascade-card/groom join first.
- No key, CPU skip decision, counter, shader change or variant request added (request delta 0).
- Required future live gate: every ON window has affected receivers/skips > 0; PNG skin/neck under hair
  neither black nor unshadowed; preserve shadows where RT falls back. No live or GPU cost measured.
- Validation: build passed; 149/150 CTests passed, water shader gate failed opening shared SQLite cache;
  that gate passed on retry with a local read-only cache snapshot (1.05 s). No commit or game launch.
