## Scene handout increment 1: update-after-bind set 1 layout (branch claude/scene-handout-1, offline-verified, 2026-11-09)
- New restart key `rt_scene_handout_layout` (default 0; docs/SCENE_HANDOUT_DESIGN.md 3.2). On: the layer asks for
  storage-buffer and sampled-image update-after-bind in the game's own descriptor-indexing (or Vulkan 1.2) features
  struct, acceleration-structure update-after-bind in its own struct; set 1 (layer/alphatest.cpp) gets
  UPDATE_AFTER_BIND on every written binding (immutable samplers excluded), its pool and the hit-material clone pools
  the UAB pool flag; an empty dummy TLAS is recorded once into the first scene build's command buffer
  (`rt_scene_dummy_tlas()`, valid once that recording was submitted). If the device creation fails with the request,
  the layer retries without it. Nothing consumes the layout or the dummy yet (increments 2-3).
- Off: no device, layout, pool or command change; no shader or variant change (shaders/ and
  common/shader_variant.h untouched, so every request is byte-identical; variants.txt delta 0). No ABI change.
- Offline: build + 222/222 CTests pass. New CTest scene_handout_gpu_fixture (RTX 5090, Khronos validation, private
  loader, no game): set 1 look-alike with the layer's flags; update after bind (stamp, one of 4096 views, TLAS),
  second update, fallback copy P -> R after bind all read back right; empty TLAS traces no hit; 0 validation errors.
  UAB sampled-image limit per stage 1048576 (set 1 needs 4176).
- Not tested in game: the feature patch on BG3's device. Check at the next live: log lines
  `[rt] + scene handout layout`, `[rt] scene handout layout on`, `[alpha] set 1 layout update-after-bind`,
  `[rt] scene handout: dummy TLAS ... recorded`, and the GPU ms of the RT passes unchanged.
