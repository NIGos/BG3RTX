## Fourth instanced family: IP grass at camp (branch hm-camp-grass, READY_FOR_LIVE, 2026-11-02)
- Camp's largest hit-material blocker (m4p1 031746: row reason 60% of lookups) is IP_Wind_GrassFlowers_AlphaTest_2S_VT /
  _PBR_VT: depth 05d39f75/73849471, G-buffer 105e47e4/35a18400 (asset DB). The G-buffer FS is the Base_VT decode
  ("same sampling", tests/material_families_test.py IpGrass) with its VT layers at 1:4..1:7 (`LayoutVt4`), engine UBO as
  the 2S family (index 44, channel 48, flag 52), reflectance at 1:1 byte 16, plus a camera-distance dither discard.
  Both VSs place the vertex with textually the same wind arithmetic: rows checked bit for bit, no tolerance.
- Key `rt_hit_material_ip_grass=0` (startup: the 105e47e4 frame patch at N location 2 and the RT_HIT_IP_GRASS variants;
  then live 0/1 of its rows; needs rt_hit_material_instanced). Family 4 in word 220 bits 8..10: a consumer without the
  define reads bits 8..9 = 0 and checks the row bit for bit with the plain rule (fail closed, uncounted); with it the
  3-bit field and clip counters 435..442 ("hit material instanced clip IP grass"). Pair check as wind plus the 2S
  translation; remap counters per family (perm base 64 -> 80).
- S6 lava (FLUID_Lava_Plane_Flowmap bd5dbda2/0b8dfd73): no small recipe. The FS animates a flowmap with frame time and
  reads a per-vertex colour, the absolute world position and 22 material floats over 4 textures; the pool row and
  basis carry none of them. Its look is mostly emission, which hidden hits already add. Not started.
- Live job `tools/live_batches/m4_ip_grass` (camp, 4.4 min, 0.2 GB), dry run passed; predicted qualified +30 points
  (10-40), shot-on null (band 3.5%).
