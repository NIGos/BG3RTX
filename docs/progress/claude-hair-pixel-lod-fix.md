## rt_hair_pixel_lod: level pick inputs fixed (branch claude/hair-pixel-lod-fix, offline, key still off, 2026-11-10)

- Cause 1 (hl1 180301/180949: 12 of 13 grooms never left level 0): `groom_r_median` (layer/bvhsplit.cpp) read the
  LOD's host radii, kept only for LODs uploaded while the key was on; for the others d0 = 0, so `pixel_lod_level`
  returned 0 and the re-decode in `groom_cam` (which needs level > 0) never ran.
  Fix: `groom_lod` sets the LOD's median radius at every upload whatever the key (`hair_groom::median_radius`, one
  float per LOD; nth_element over the decoded radii already in memory). The period line's LOD 0 diameter is then never
  0 for an uploaded LOD; host radii remain only a fallback.
- Cause 2: "head px" was the groom bounds diagonal / distance (177-180 px for a 64 px head), so the study's 60 px floor
  applied about 2x too low. Fix: head px = the study's head height 0.25 m (`kPixelLodHeadM`) / distance x px per
  radian (`hair_groom::pixel_lod_head_px`); the floor keeps its 60 px, now on the quantity it was derived for.
  px per radian (1025 in S8) was already right.
- Test (hair_groom_asset_test): median radius from the decoded radii only (no host arrays); a 64 px head at 4.0 m with
  0.186 mm strands gives d0 0.048 px and level 5 (> 0); a 59 px head keeps level 0 although its diagonal is over 60 px.
- No shader, no ABI, no settings change; variants delta 0. CPU cost: one nth_element per LOD upload (under 1 ms for
  0.9 M points, inside the 30-80 ms decode).
- Expected in the next pixel LOD live (pl-on): d0 range without 0.00, head px 0.25 m-based (front S8 head about 64),
  most medium heads at levels 3-5, heads under 60 px at level 0. Status: key off, READY_FOR_LIVE with the next hl1.
