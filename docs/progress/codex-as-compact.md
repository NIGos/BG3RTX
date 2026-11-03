## BLAS chunk reclamation (branch codex-as-compact, READY_FOR_LIVE, 2026-11-02)

- Implemented the permitted small fallback: always retire wholly empty chunks and allocate new BLAS
  first-fit in fullest chunks. No guard-low dependency, new key, shader, ABI or variant change (delta 0).
- Live bytes exclude pending frees; occupied bytes retain them until 6 presents, frame + 2 GPU completion,
  and retirement of all old TLAS slots. Removed the unsafe 240-present fallback for these ranges.
- Stable chunk slots are reused without moving indices. Failed AS creation rolls occupancy back.
  Retained diagnostic handles pin their own backing chunk even after rt_as_keep is switched off.
- General relocation deferred: existing COMPACT swaps own cached meshes only; dynamic, cascade and sun
  clone BLAS need additional owner/copy lifetime handling. No arbitrary reserved/live trigger is added.
  TLAS records take current addresses each build; address changes force BUILD through the signature.
- [bvh] mesh cache logs active chunks, reserved/live/pending MB, window freed count/MB and relocated 0.
  Freed means enqueued for existing deferred destruction. [mem] ours live excludes pending bytes.
- Offline: host DLL + allocator/guard tests built; 93 selected non-shader CTests passed; final allocator
  test 46 checks passed. Full build avoided because its ALL GPU fixture target invokes DXC. No shader
  tools, GPU tests, game launch, git metadata writes or commits. git diff --check passed.
- CPU policy only, no added GPU commands. Actual VRAM savings and GPU ms remain unmeasured. Expected:
  empty unpinned chunks disappear after retirement even with rt_vram_guard=0, and partly live chunks stay.
  Verify in a separately authorized S8 low long-view batch with a scene transition and GPU backlog.
- Design/limits: docs/TRACK_D_SCENE_BUILD.md, BLAS chunk reclamation section. No quality pass claimed.
