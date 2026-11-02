## Game data audit: what each RT feature reads, and from where (branch worktree-agent-a1f09aa8ed1b0702d, docs only, 2026-11-02)
- New docs/GAME_DATA_AUDIT.md: per feature the data it needs, its source (runtime / asset DB / hand constant), live
  coverage, better game data not used, licensing, and a ranked action list. No code change.
- Already exact: water compose (FS's own N and DFG LUT), probes/ambient/DFG, b25/b29 lights, sun colour and clouds,
  XFB geometry (skinning, wind, terrain), non-VT alpha, emissive exact families.
- Replaceable by exact game data: VT alpha from the page cache (1.48% of camp alpha test on coarser pages), lava heat
  constant 0.5, roof volume pads (block 85% of S8 sun-lit far hits), rho x receiver ambient at far hits, class 4
  front lobe only. Schlick survives only in the water_reflect diagnostic.
- Top 5: (1) VT masks for every VT alpha-test material, enumerated from the asset DB (the extractor reads one replay
  pack and a hard-coded LSLib path today); (2) recipe rows from the DB's bindings + classifier; (3) cascade caster
  roofs instead of volumes for GI far rays; (4) vertex COLOR in the XFB stream (Tileset rows, lava heat, Tileset
  BGM); (5) class 4 transmission at hits.
- Licensing: masks, DB and any GR2/texture extraction are generated on the user's machine (LSLib MIT or a C++ port).
