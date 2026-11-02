# Original launcher ornament study

Branch `ux-ornaments`, 2026-11-02. Original project artwork for the offline
launcher mockup. Licence: **project's own**. No third-party artwork is included.
The project's broader distribution licence remains pending; this directory does
not choose an open-source licence or change the separate bundled font licence.

## Assets

| File | Intrinsic size | Origin and use |
| --- | --- | --- |
| `corner.svg` | 112 x 112 | Authored stroked gold-gradient stems and curls. Rotate 0/90/180/270 degrees for four corners. |
| `divider.svg` | 480 x 28 | Authored entwined linework with a faceted amber gem. |
| `frame.svg` | 96 x 96 | Authored nine-slice brass border. CSS `border-image: url(...) 24 / 12px / 0 stretch`. Transparent centre. |
| `dark-stone.svg` | 1024 x 1024 | Procedural SVG fractal grain, seed 417, dark vignette. Outer surfaces only. |
| `parchment-panel.svg` | 512 x 512 | Procedural warm parchment wash and fibre grain, seed 87. Mockup masks it under a nearly opaque charcoal layer for small-copy readability. |
| `glyph-light.svg` | 24 x 24 | Authored meaningless abstract strokes. |
| `glyph-water.svg` | 24 x 24 | Authored meaningless abstract strokes. |
| `glyph-performance.svg` | 24 x 24 | Authored meaningless abstract strokes. |
| `glyph-memory.svg` | 24 x 24 | Authored meaningless abstract strokes. |
| `glyph-help.svg` | 24 x 24 | Authored meaningless abstract strokes. |
| `hero-lantern.png` | 1536 x 1024 | AI-generated with the built-in image_gen tool. Painterly lantern at dusk; prompt below. |

The SVGs are editable sources, not AI-generated. Texture filters have explicit
seeds and need no downloaded material or generation package. Glyphs have no
language or lore meaning; they are not copied from any real or fictional alphabet.
The existing BG3RTX split-lantern mark remains the identity, unchanged.

## AI-generated hero prompt

> Use case: stylized-concept. Asset type: original painterly landscape hero illustration for an independent fantasy lighting mod launcher. Wide 1536x1024 painting, composed to crop to a shallow panoramic banner. A single handcrafted brass lantern with simple geometric metalwork and warm amber light rests on an old rain-dark stone ledge on the right third. Quiet blue dusk, hazy mountains and a few indistinct ruined stone arches far away, soft painterly brushwork, rich warm gold against deep slate and indigo, understated luminous atmosphere. Keep the left half dark and uncluttered for separately rendered UI text. No text anywhere, no UI, no borders, no logos, no watermark, no letters, no runes, no emblem. Entirely original generic high-fantasy environment. Do not copy or imitate any actual game artwork, Larian or Wizards of the Coast artwork or fonts or trademarks. No dragons, mind-flayers, tadpoles, nautiloids, Forgotten Realms symbols, D&D ampersand, or recognizable franchise motifs. Save as a project asset; return the saved local file path if available.

## Typography and accessibility

The mockup loads existing local `assets/fonts/LatoWeb-Regular.ttf` and
`LatoWeb-Semibold.ttf`; no network fonts, scripts or images. Display typography
uses local `Palatino Linotype`, `Palatino`, `Georgia`, then `serif`. For a future
OFL display face, evaluate **Alegreya** (SIL OFL 1.1), an independent humanist
serif. It is a recommendation for future bundling, not included or required here;
verify its upstream licence/version and preserve its licence before distributing.
No franchise fonts are used, traced or requested.

This intentionally richer study adds ornament around surfaces while keeping
body text quiet. All decorative images have empty alternative text or are CSS
backgrounds. Switches retain native keyboard input and explicit labels; focus
rings, reduced motion and forced-colors fallbacks are present. Section names
remain written text beside decorative glyphs. Reflow retains navigation on
narrow screens. This is an art-direction mockup: preset buttons and Save/Play
are previews, not connected launcher operations; displayed costs are sample data.
A browser screenshot does not establish native launcher DPI or screen-reader QA.
