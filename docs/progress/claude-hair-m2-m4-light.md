## Strand AO transmittance and fibre specular indirect (branch claude/hair-m2-m4-light, READY_FOR_LIVE, 2026-11-07)
- M2.3 `rt_hair_groom_ao_t` (spec 1044, knob exp_hair_groom_ao_t, off): GI trace and RT-hit AO rays keep the
  closest strand hit with probability 1 - d_f lum(a_f^n) (rt_groom_blocks), else pass on. Before: strands opaque.
  Layer packs a_f when shadow_t or ao_t is on (restart for the layer).
- M4.2 `rt_hair_fibre_spec` (knob exp_hair_fibre_spec, off): new pass gi/hair_fibre_spec.hlsl after D-RF replaces
  the game's class-5 probe term (transcribed from 3033c895) with the R lobe: one exact lobe sample (hb_sample_r),
  on-screen radiance (D-RF tier a) or the game's probes in that direction.
- Gates: AO bench crossings 3000/3000, closest t error 9.8e-8 m, occlusion 1767.65 vs 1765.57 (2.0 sigma);
  R-lobe furnace worst 0.30% (limit 3%); GPU vs CPU direction p99 9.1e-7, weight 2.4e-5 relative.
- Identity vs main: 160/210 byte-identical, 50 equivalent at default (1026/1044 = 0), 0 failed; hair_fibre 3/3
  identical. Request delta: variants.txt +2 (hair_fibre_spec with/without OMM). Spec id 1044 (1042/1043 are stream A).
- CTests: 206/207 full run, the failure (core_resource_init_test: ensure_rf_layout fixture) fixed and rerun green.
- Cost unmeasured (lives paused). Predicted L2: skin/neck under hair brighter in GI AO with ao_t on (fixture: 22%
  less strand AO at a_f 0.5). Predicted L3: strand specular changes only on strand pixels, mean within the game's
  term where tier (a) is rare; check the Tp transcription with key on vs off on a static camera (no tier a hits:
  difference = A_0 probe(wi) - Tp only).
