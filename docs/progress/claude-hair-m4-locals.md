## Strand fibre shading for shadow-mapped local lights (branch claude/hair-m4-locals, READY_FOR_LIVE, 2026-11-08)
- M4.3 `rt_hair_fibre_mapped` (spec 1030, knob exp_hair_fibre_mapped, off; needs rt_hair_fibre=1): gi/hair_fibre.hlsl
  now also replaces the game's hair term of lights with a game shadow map (E S with the cards' atlas term, transcribed
  from 3033c895) by the fibre response with the traced visibility and the strand count, where D-LS trusts the trace.
- Gates: removed game term GPU vs CPU twin max 5.4e-6 relative; N-light fixture (20,000 lights) white single
  scattering 0.99973 / 0.99997 (analytic 1); dual sums GPU = CPU within 3.4e-7.
- Identity vs main 7f86f343: 218/221 byte-identical, 3 hair_fibre requests equivalent at 1030 = 0; request delta 0.
- CTests: 207/208 on a fresh VS 2022 build tree; the stale schema was regenerated and settings_test passes.
- Cost unmeasured: one atlas PCF + scene ray + groom query per strand pixel and mapped light (as an unmapped light).
- Check: a torch-lit scene (Test Dialogue1, Campsite), hair under shadow-mapped lights, key on vs off in one run.
