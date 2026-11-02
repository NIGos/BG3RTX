## Original fantasy launcher ornament study (branch ux-ornaments, review candidate, 2026-11-02)
- Added 10 original SVG ornaments/textures/glyphs and one AI-generated 1536 x 1024 lantern hero; provenance and prompt are in assets/branding/ornaments/README.md.
- Updated the existing HTML mockup with a nine-slice brass window frame, card corners, gem dividers and switches, illuminated headings and engraved ember Play.
- Removed 2 network font references; uses bundled Lato and local serif fallbacks. Existing project mark is unchanged; no franchise artwork or font is used.
- Keyboard switches have labels and focus; reduced-motion, forced-colors and narrow navigation layouts are present. Native launcher DPI and screen-reader QA remain separate.
- Browser checks: all local images/fonts loaded; no external requests and no document/main horizontal overflow at 1200, 800, 720 and 400 px. Native launcher DPI and screen-reader QA are not claimed.
- Final desktop fit: main client/scroll height 711/711 px; Experimental visible. Space toggles a focused switch; forced-colors restores native checkboxes and removes textures/corners. Text palette pairs checked have a minimum 5.52:1 contrast. Screenshot: 800 x 600 px.
- Review: open docs/release/launcher_mockup/index.html offline; compare the original mockup-800.jpg with mockup-bg3style-800.png. Costs shown are unchanged sample data, not measurements.
- No build, renderer, ABI, settings, presets or packaging changes; shader request delta 0. Git metadata is read-only; no commit or merge.
