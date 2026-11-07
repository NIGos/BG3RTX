## Strand grain attribution (branch claude/hair-grain, design only, 2026-11-07)
- New tool `tools/hair_groom/grain_stages.py`: per image-chain stage (G-buffer RT1-RT3, motion, depth, deferred
  lighting, compose, RR guides, RR input/output, final), HF energy and line coherence over the class-5 hair mask.
- On the 2026-11-07 Test Dialogue1 traces (strands a, f, fp0, fp2 vs cards e; a/e matched lit mean 0.4165/0.4169):
  strands never carry more per-frame HF than cards (a/e: albedo 0.99, lit 0.91, RR output 0.78, final 0.96);
  motion vectors vary 2.1x more per pixel but have the cards' outlier rate (1.1-1.2% > 0.5 px).
- What differs is line structure: coherence albedo 0.31-0.33 vs 0.35, lighting 0.28 vs 0.32 (gap 4x the strand
  repeat spread), final 0.75-0.79 vs 0.84. Cause attributed to the fibre normal (N.V = 1 on every strand pixel,
  `hair_primary_gbuffer.hlsl:248/294`): the game's wrap diffuse, ambient and RR normal/specular guides lose the
  clump-aligned shading the card normal gives.
- Proposed `rt_hair_macro_normal` (RT0 = card normal, tangent unchanged), design in RTX_HAIR_PRIMARY 5g; not
  implemented (needs plumbing and a live A/B; the RT0 preview is too coarse for an offline proof).
