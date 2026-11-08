## Strand fibre shading: review fixes 7-11 (branch claude/hair-fibre-fix, offline, READY_FOR_LIVE, 2026-11-08)
- Question for the user: the dual model cannot match the path-traced hair at both normal and diffuse incidence
  with one k. Taken: the diffuse fit (energy of a lit hair volume conserved). Hair under rt_hair_fibre reads
  ~0.76-0.91 of the normal-incidence reference (was 1.02) instead of 1.3-2.2x too much energy at diffuse
  incidence. Alternative: a k(mu_i) table from the strand macro normal (more work, needs rt_hair_macro_normal).
- 7 confirmed: k multiplied T_f f_s (a fibre behind one strand: up to 2x the f_s of a directly lit one). k now
  scales only the backward multiple scattering (`dual.single_part`, `hd_parts`). k refitted on the path-traced
  diffuse slab (`dual.make_table`): every grid entry equals the walks within 0.03%, white (c 0.9) 0.894-0.903 < 1,
  CHAR_Hair held out 1.001/1.002/0.998; k monotonic in roughness. The old zig-zag in colour (2.43, 3.55, 2.51) was
  the normal-incidence walk's heavy-tailed weights (12000-path walks at c 0.7, beta 0.5: 0.083..0.134).
- 8 confirmed: hair_fibre subtracted a re-derived D-LS term. It now replays D-LS's ray (`hf_ray`: trace_light's
  origin, soft sample, extent) and strand decision (`hf_groom`: rt_groom_blocks' draw, the groom's a_f) and counts
  n on that ray. rt_hair_fibre=2 (constant 1046) is the in-frame check: the game's terms at the fibre's
  transmittance model on the D-LS-traced lights; strand pixels must equal key off (exact without
  rt_hair_groom_shadow_t, mean equal with it). Limit: the physical hair origin (1009) is not in hair_fibre's request.
- 9 confirmed: all fibre light went to the diffuse buffer. Single scattering (all lobes) now to specular, where the
  game's R/TT/TRT are; multiple scattering to diffuse.
- 10 confirmed (and wider): no replaced term took the AO-on-direct factor. `hf_ao`: the game's factor, D-LS's
  ao_direct give-back for traced lights and the sun; the fibre response takes the factor of the term it replaces.
- 11 rejected: Lrt is last frame's scene radiance (D-RF uses it the same way); k = lScale 100 converts probe units
  and SO is the probes' occlusion, so neither applies to a traced hit.
- Gates: CPU test_dual/test_volume pass (new: furnace, offset-independence of the k part, single n=1 <= n=0 on
  every entry); GPU dual check adds the split and n=1 <= n=0 on every entry. Shader identity vs main: 216/221;
  different only gi/hair_fibre (3, fibre key) and rt/hair_primary_count (k is in the same constant array it reads
  a_f from; the a_f values are unchanged). A separate k array read shifted entries on the GPU (DXC SPIR-V; the GPU
  check caught it), so the table stays one array. Request delta 0.
- Live check (when allowed): one scene with hair, rt_hair_fibre 0/2 toggled in one run: strand-pixel mean equal; then
  1 vs 0 for the look; GPU ms of the hair fibre pass (inside D-LS's timed region).
