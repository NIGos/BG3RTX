## Strand fibre passes now fire: strand-pixel depth test, sun mask (branch claude/hair-fix3-fibre, offline, READY_FOR_LIVE, 2026-11-08)
- H1 confirmed: hf_strand (gi/hair_strand_pixel.hlsli) and hv_strand_pixel (rt/hair_fibre_gi.hlsli) compared the entry's
  device depth e.z with gDepth's linear depth at 1e-6 relative: no pixel ever matched, so rt_hair_fibre, _spec and _gi
  returned on every pixel and the rt_hair_fibre=2 check of claude/hair-fibre-fix passed vacuously.
- Fix: shaders/rt/hair_depth_match.hlsli compares as device depths, d -> 0.1 / (d zr) (reversed infinite Z, near 0.1;
  zr the frustum rays' view depth), tolerance one D24 code plus 6 fp32 half ulps relative (derivation in the file).
- GPU test (hair_primary_gpu_test depth_match_check): 17810 real strand pixels of the GPU strand pass, game depth chain
  (z = 0.1 / w, D24 code, R32F linear, zr 1 and 1000): match 100%; twice the tolerance away 0%; the old rule 0%.
- M3 confirmed: the sun's fibre response ignored the game's mask m (sun under roofs once H1 works). Now E m F where our
  scene ray is lit (min(m, v)); n from the groom query only sets T_f.
- Identity vs main: 213/221; different only gi/hair_fibre (3), gi/hair_fibre_spec (3) and gi_composite's
  RT_HAIR_FIBRE_GI_REFERENCE variants (2), all behind fibre keys. Request delta 0.
- Cost: the passes now run on every strand pixel (one scene ray and one groom query per D-LS-traced light and the sun;
  one reflection ray for _spec). Not measured: needs the live cost window (the hair fibre GPU ms inside D-LS's region).
- Live check (when allowed): rt_hair_fibre 0/2 in one run, strand-pixel mean equal (now a real comparison); then 1 vs 0
  and the GPU ms.
