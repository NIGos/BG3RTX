## Strand coverage from the pixel record, two-sided bound (branch claude/hair-pixel-coverage, READY_FOR_LIVE, 2026-11-04)

- hv7 09:31: strand/card 2.55 on the close-up: the strand occlusion query (masked probe, swapped variant) counted every
  overlapping card layer's fragment; the prepass card query (depth writes) far fewer. Not a coverage measure.
- Now the strand side is the pixel record: unique strand pixels per groom of the counted G-buffer frame (one entry per
  pixel), mapped to the draw key of the header that frame applied, counted with that frame's tag (the counter had used
  a newer publish's tag). The G-buffer strand queries are removed; the probe still draws (it writes the entries).
- Bound two-sided: [0.891, 1.094] (offline min 0.911 / max 1.075 over 2631 views, minus / plus the modelled noise).
  Gates: queue_gates items.upper; hv6 coverage gate; hv7 prepass count any (the prepass swap follow-up).
- Predicted on the Dialogue1 close-up: 0.80-1.05 (the prepass card samples may still count a few stacked layers: a
  ratio just under 0.891 then keeps cards, fail closed). Far LOD2 draws: low ratios, cards stay.
