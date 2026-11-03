## Animated materials follow-up (branch anim-materials, PARTIAL_IMPLEMENTATION, 2026-11-03)
- Base WIP 8480a153; no new commit or git metadata writes. No game or saves touched.
- Key rt_hit_material_animated stays 0 in every preset; ABI delta 0, request delta 0.
- Moved animation from reserved SpecId 1021 to 1102 (1021..1024, 1100/1101 taken).
- Outer specialization guard removes animation reads/rejection when off.
- Added spec_cascade_check --key animated: every hit request, literal OFF/ON control,
  and pre-WIP ab7d1df7eea4 baseline; instancing off/on, Wind RGB, and IP grass states.
- Shader PASS: 45/45 requests; 180/180 key-off baseline + 360/360 literal OFF/ON cases.
- CPU tests: 720 scroll/flow/bounce/atlas cases plus 192 native sprite hash/frame cases.
- Sprite audit now includes BaseInstance, engine seed, nested hash, round-even phase,
  native multiply order, fused frame calculation and truncating atlas remainder.
- Native time is 0:0 byte 0 for named scrolling, ENV bounce lava and sprite templates.
- Publication remains scrolling only, with current native resources/time and stale rejection.
- Lava stays off: packs lack current COLOR/noise, flow binding and displaced geometry/basis.
- Sprite stays off: no native atlas/instance seed plus displaced billboard geometry witness.
- No invented clock for missing/unclassified templates; no generic animation coverage claim.
- S6 lava + camp drafts and predicted.json retained outside pending; remain BLOCKED_IMPLEMENTATION.
- Full CTests caught missing exposure-held expectations in both drafts; repaired.
- Build + 138/138 CTests PASS; dryrun: 2 strict rejections + 2 structural passes; diff check PASS.
- GPU cost/visible gain unmeasured; no quality pass, Ultra promotion or live request.
