## Animated material audit (branch anim-materials, PARTIAL_IMPLEMENTATION, 2026-11-03)
- No game, saves, git metadata writes, commit or shared asset DB modifications.
- Full census: 1,063 templates / 32,696 instances; 630 / 7,172 read game time.
- Capabilities overlap: scroll 459/5,532; flow 62/378; sprite 47/700;
  wind/cloth 88/729; pulse 68/958; displacement 182/2,403; water 20/412.
- Four missing templates, 73 unclassified time users; switches may disable an instance.
- Compact v4: 40,983 rows; scroll 5, flow 44, pulse 5, displacement 4;
  sprite/wind/water 0. Per-level counts and exact scopes: ANIMATED_MATERIALS.md.
- Time: native set 0 binding 0 byte 0 float, as existing D-EM pulse.
- Added rt_hit_material_animated, off in every preset, GUI control; spec 1021.
- Publishes only Base_VT_Scrolling with native views/basis and current draw time.
- Missing/nonfinite/stale time declines; per-axis VT gradients preserve tiling.
- Shared CPU/HLSL lava flow/bounce and sprite atlas math; 180 reference cases pass.
- Lava/sprite publication NOT implemented; native resource/geometry paths remain missing.
- S6/camp/S8 packs lack original lava time/UBO/flow bindings; comparison unavailable.
- Six focused CTests pass; host/core build passes; 12 hit shaders compile and validate.
- Three temporal requests byte-identical to HEAD; hit-module baseline identity
  FAILED (0/4 identical). Spec 1021 changes those modules; no key-off parity claim.
- Unqueued S6 + camp draft: 130 s/scene, 740 MB estimated; fake-runner dryrun passes.
- Draft is BLOCKED_IMPLEMENTATION, not READY_FOR_LIVE. GPU cost/gain unmeasured.
- ABI delta 0; shader request delta 0. No promotion or generic/lava quality pass.
