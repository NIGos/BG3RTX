## Pixel-footprint strand LOD for camera rays: (a), (b), (d), (e) built, (c) open (branch claude/hair-pixel-lod, offline, 2026-11-10)

- Key rt_hair_pixel_lod (dev, off, pinned in release.ini), rt_hair_pixel_lod_k 1.0. Design and details:
  docs/RTX_HAIR_PRIMARY.md 5v.
- Step 1: decode_v3_lod(..., extra_keep) rebuilds keeps 1/8, 1/16, 1/32 from LOD 0 (nested hash, radii x 1/keep, 9 mm
  cap); no file change. hair_groom_v3_test: all three non-empty, nested, radii exact to 1e-6.
- (a) 2a43c06b: pixel_lod_level (common/hair_groom_budget.h): finest lod_keep / 2^j reaching k px, 1/32 floor, level 0
  under 60 px heads, quarter-octave hysteresis; unit tests.
- (b) edb6103e: camera arrays per (LOD, level) (hair_groom::camera_subset: kept strands' segment starts, radii x 2^j),
  a camera BLAS per groom slot on its points (refit with the groom BLAS), a camera groom TLAS per scene slot at the
  groom TLAS rows; the header names it, row.z = the camera index buffer. Shadow, GI and fibre rays keep the groom TLAS.
  Every pixel-record segment reader resolves through the header row (checked). Key off: no allocation, unchanged.
  Shader identity 227/227 vs e5f62b50, variants delta 0. hair_groom_v3_test: 14 LOD x level pairs equal to the
  decode-time keeps.
- (d) 3b501d8e: period line `[hair groom] pixel LOD` (levels, diameter and head px, px per radian, camera builds, arrays
  and BLAS MB). Pool MB, BLAS MB, refit ms: not measured offline (no layer BLAS fixture); the live part reads them.
- (e) 8cff5e87: no separate job; hl1-s8-late-arrival (hair_late) gains part 2 after its unchanged arrival part:
  pl-off / pl-on / pl-off2 (60-frame windows), PNG bursts 99310/99311, period line and strand G-buffer GPU ms per
  window; predictions in predicted.json part2_rt_hair_pixel_lod. live_flow --frozen 0 failing. Staged, not queued.
- Left: (c) cross-fade between adjacent levels (spec constant 1048), design on the GPU hair fixture first; without it a
  level change pops (about 2x a LOD step). No launcher toggle yet. The projection read (UBO float 21) is confirmed only
  by the live period line.
