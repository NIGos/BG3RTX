## Elastic capture reserves (branch codex-capture-reserves, PARTIAL, 2026-11-02)

- `rt_capture_elastic=0` by default. Production changes only in geocapture.cpp and
  rt_capture_budget.h; supporting settings, schema and CMake-registered CPU tests.
- Demand sizing reuses the cascade's 25% headroom and 16 MiB quantum. Lower-demand
  window: 24 main captures per buffer (three eight-slot scene rings; 72 scene
  frames for a cascade slot). Peaks are retained until replacement succeeds.
- Synthetic vg1 cascade demand 169 MiB -> 224 MiB per slot, vs 560 MiB reserved:
  potential 1008 MiB reduction across three buffers. Not measured in game.
- RT demand includes rejected opaque bytes, alpha demand and partition floors.
  Synthetic 1182 MiB opaque plus 169 MiB alpha reaches the 2047 MiB cap, retaining
  room for both streams. Growth requires the active VRAM guard and its grant;
  uses its card-scaled cascade cap with the RT startup size as floor.
- Replacement is transactional and checks GPU retirement. Capacity lines report
  targets, pending retirement and successful replacements; [mem] ours reads the
  actual buffer sizes. Key off keeps the original sizing/growth path.
- LIMIT: strict retirement can starve replacement during continuous capture,
  especially the singleton RT buffer. This does not yet guarantee growth during
  a camera turn. A question about deferred RT replacement is pending. The
  experimental current-sun private cascade buffers retain their existing policy.
- Verified: layer C++ build; capture budget, elastic capture, VRAM guard and
  settings CTests (4/4). No game, shader compile/identity or spec checks run.
  Shader variant request delta 0; ABI unchanged; no commit (metadata read-only).
- GPU cost and real memory savings unmeasured. Not READY_FOR_LIVE or a quality
  pass until the continuous-capture retirement limitation is resolved.
