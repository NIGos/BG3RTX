# Original charcoal surface recipe

`scripts/export-branding.py`, function `surface`, is the source recipe.
It creates `surface-charcoal.png`: 256 x 256 RGB, base (23, 25, 29), seed
`0xB13A`. For each integer pixel (x, y), compute with unsigned 32-bit wrap:

```text
n = (x * 374761393 + y * 668265263) XOR 0xB13A
n = (n XOR (n >> 13)) * 1274126177
d = ((n XOR (n >> 16)) mod 5) - 2
RGB = (23 + d, 25 + d, 29 + d)
```

There is no external image, sampled material, game data, baked lighting,
animation or nondeterministic random state. The tile repeats every 256 pixels
when tiled by the caller; it has no geometric veins to align at the seams.
The recipe was authored for this task. Use only on header/outer background
regions, at native logical scale or lower opacity. Keep text panels flat.
The five-level +/-2 noise is deliberately subtle, not a stone photograph.
In system high contrast omit it entirely and use system surface colours.

The two SVG masters and this recipe are original project artwork. They use no
third-party artwork or font outlines. Their distribution follows the project's
licence decision; this task does not assign a new project licence. The font
files have their own separate OFL licence.
