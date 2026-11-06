## DLSS 5 offline defect review (branch review-dlss5, FIXED_OFFLINE, 2026-11-06)
- Fixed Streamline deferred-init race: pending scratch check/move and trigger logging share the mutex.
- Fixed stale initialized state after device destruction: cleared SDK callbacks can no longer be
  called by a replacement device; pending scratch, instance, requirements and OF queue state reset.
- Regression executes the actual teardown body with a deferred initialization and a foreign device:
  previous body fails (exit 2); fixed body passes. Existing trigger decision coverage retained.
- Reviewed NR boundary, guide/codec, render-size retirement, bypass, device requirements, runtime
  failure fallback, FG and pre-post HDR contract. No further confirmed defects in these paths.
- Private images remain bounded at 8 x 210.9 MiB for 3840 x 1600, plus NGX history; no new allocations.
- Unconfirmed runtime impact, unchanged: partial NGX hook-install failure can leave shutdown hooks
  installed; NR device teardown destroys bound images before the last feature release.
- Shader sources and manifest unchanged (request delta 0). No game/queue/Git writes; no GPU cost measured.
- Validation: full build + CTest 197/197 (434.18 s); rebuilt final layer + targeted checks 5/5.
  Teardown regression fails before the fix and passes after. No live FG/HDR/recreation qualification.
