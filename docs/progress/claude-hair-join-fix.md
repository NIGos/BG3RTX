## Hair grooms never joined: BVH split ring failed in the BAR heap (branch claude/hair-join-fix, READY_FOR_LIVE, 2026-11-07)

- Symptom (user run 2026-11-07 18:02, Test Dialogue1, and five runner logs): no `[hair groom] ... LOD n uploaded`,
  no `draw with N indices`, no `unmatched hair draw`, no `groom instances` stats line.
- Cause: every one of those logs has `[bvh] allocation failed (N instances); the split is off, the legacy build
  takes over` at the first scene build. Groom joins, uploads and the stats line exist only in the split path
  (layer/bvhsplit.cpp build: ensure_ring at :2372 on main), so no groom ever joins. No ring line and no
  fault range for "instance/InstInfo ring": make_buf failed in AllocateMemory for a 2.9 MB host buffer.
- Why now: after the 2026-11-07 PC reinstall Resizable BAR is off. vulkaninfo: the DEVICE_LOCAL|HOST_VISIBLE
  type sits in a separate 214 MiB heap (157 MiB budget on the desktop); the `[mem]` heaps sum went from
  32187 MB (2026-11-06) to 32404 MB (+214). make_buf (bvhsplit.cpp:137, unchanged since 2026-10-26) asked
  for that type and fell back to system memory only when the type did not exist, never when its heap was full.
  Not a code regression after hv9: the same code drew strands on 2026-11-04 with ReBAR on.
- Fix: `common/host_memory.h` allocate_host_visible: BAR VRAM first, system memory when that allocation
  fails. Used by bvhsplit make_buf and hit_material create (same pattern). With room in the BAR heap the
  choice is unchanged. No shader, key or ABI change.
- Diagnostics: `[bvh] host-visible VRAM (BAR heap) full: <buffer> (<KB>) in system memory` (first 8);
  `[hair groom] join gate: rt_hair_groom 1, linear swept spheres L, native hair capture N -> groom joins on/OFF`
  on each gate change; the allocation-failed line now names ring/TLAS state and the groom consequence.
- Test: CTest host_memory_test (the user's no-ReBAR memory table: BAR full -> system type, ReBAR unchanged).
- Next live (same save, same keys): expect `[bvh] instance ring: 4096 instances` and the 8 `TLAS storage`
  lines, probably one or more `BAR heap full` lines, `join gate ... -> groom joins on`, no `[bvh] allocation
  failed`, then `[hair groom] <id> LOD n uploaded` and `frame N: K groom instances ... matched M` with K, M > 0.
  Alternative without a build: enable Resizable BAR in the BIOS.
