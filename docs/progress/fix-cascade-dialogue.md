## Cascade dialogue duplicate fix (branch fix-cascade-dialogue, READY_FOR_LIVE, 2026-11-03)
- No commit: git metadata read-only. No game launch or save access.
- Root cause: placement UBO offsets in the full draw key let a camera-visible
  opaque character enter the legacy F-1/F-2 cascade soup as a second pose.
- Fix directly on the default path: same-frame camera mesh + material + instance
  matching independent of per-draw UBO allocation. No new key.
- Audited class-B layouts supply the model matrix and pulled vertex index.
  Material binding, mesh/range, instance count/first instance and pulled stream
  remain part of identity. Class-A instance-stream ranges remain exact.
- Model translations are compared in absolute world space using each pass's
  origin. Comparison uncertainty derives from float encoding precision.
- Unknown layouts, unavailable mapped data and unmatched off-camera roofs retain
  existing admission. Raw Current Sun geometry and coverage proof stay unchanged;
  its compacted legacy ranges use the corrected duplicate policy.
- `[cascade] casters` adds `duplicates by mesh (offset differs)` per frame.
- `rt_cascade_static_only=0` remains the off-by-default fallback, Developer /
  Static roof casters only (Live). Requires Sun shadows / Ray-traced + hidden
  roofs. It can omit unrecognised static roofs when enabled.
- Unit regressions cover offset/allocation mismatches, differing pass origins,
  same mesh at distinct placements, materials, pulled instances/ranges, frame
  isolation, unknown inputs and cascade-only roofs.
- Validation: C++ build passed (17 s). Full suite 114/116; fold_progress failed
  during a documentation encoding edit, then passed after correction (1/1).
  Remaining failure: shader_identity_test sees pre-existing CRLF common/settings.h;
  bytes equal HEAD after newline normalization. Unrelated file left untouched.
  Cascade regression, roof cap, GPU fixture and both replay tests passed.
- ABI unchanged; shader edits 0; shaders/variants.txt request delta 0.
- Live confirmation remains pending. Check Test Dialogue3 at the port and
  Test Dialogue1 at camp: Sun shadows / Ray-traced + hidden roofs, fallback OFF, moving face band
  absent; the new duplicate counter should increase for offset-mismatched draws.
- Outdoor/roof confirmation: S8 roofs/canopies and S7 cut-away interior retain
  coverage; smoke_ultra includes qualified keys and water composition checks.
- Matching remains conservative when cascade recording precedes its camera draw
  or the material binding is missing/different; such copies can still survive.
- GPU cost unmeasured (no new pass); use existing captured-pass and gpu lines.
