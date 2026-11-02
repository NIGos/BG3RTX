## Original visual assets (branch ux-b1-branding, kept offline, 2026-11-02)

- Added original split-lantern SVG, optical small master, black/white variants.
- Exported ICO with 16/20/24/32/40/48/64/128/256 px RGBA frames and PNGs up to 512 px.
- Added deterministic polygon exporter, SHA-256 manifest and original charcoal recipe.
- Bundled unmodified local Lato Web Regular/Semibold 2.015 under SIL OFL 1.1;
  included copyright, full licence and hashes. Source Sans 3 unavailable locally.
- `python scripts/export-branding.py --check`: exact regeneration and nine ICO
  RGBA round trips pass; font/licence hash checks included.
- Reviewed light/dark sheet at 16/32/48/256 px: clear aperture/beam and no matte halo.
  Use black mono on light surfaces and white/system foreground on dark surfaces.
- Stable A/C paths, reproduction and remaining UI QA: docs/release/VISUAL_STYLE.md.
- No resource/build/package integration; those inputs remain with C.
- No game, installer, saves, shader tools or network. Shader request delta 0.
- No commit: git metadata is read-only. Artwork follows pending project licence.
