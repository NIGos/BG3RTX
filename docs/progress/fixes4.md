## Live-analysis corrections (branch fixes4, READY_FOR_LIVE, 2026-11-03)
- Cascade shrink retains 120 nonempty samples instead of 24; zero demand retains allocation.
  Regression covers the 256 MiB buffer / 271 MiB turn demand and inactive slots.
- Compose hit shading with shift retains one F-1 TLAS, material generation and lighting tuple.
  Failed retention/material validation preserves ordinary compose. Keys remain experimental/off.
  Shared light bindings: differing F-1/forward lists preserve the native local-shadow term.
- Smoke warmup is 45 s; offscreen warmup enables hit shading before captures.
- Edge attribution uses relative depth discontinuities with D24/R32F precision bounds.
  Presents are sorted within each capture, including p128 starts. Per-frame registration
  excludes cuts; four consecutive frames and registered material endpoints are required.
  Crash exceptions are forbidden; mesh-pool warnings no longer invalidate this attribution job.
- Q/E held-key turns replace mouse turns in wf9/offscreen; camera probe adds a key-turn window.
- Validation: full build and 129/129 CTests passed; final edge CTest rerun passed (11 fixtures).
  Camera-plan checks: 617 passed. All five requested offline fake-queue dryruns passed.
  git diff --check passed. Shader requests delta 0; no shaders or ABI changed.
- No game launch, git metadata mutation or commit. Existing resolve-pp-diff.json left untouched.
- GPU cost, cascade overflow and nonzero F-1 hit attempts remain unmeasured: confirm with
  the prepared S8 smoke/offscreen/wf9 batches after explicit live authorization; layer restart needed.
