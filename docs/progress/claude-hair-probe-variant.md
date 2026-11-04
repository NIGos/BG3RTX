## Strand coverage counted by the strand G-buffer variant itself (branch claude/hair-probe-variant, READY_FOR_LIVE, 2026-11-04)

- hv6 05:15: every probe 0 strand samples (e.g. 0/102163) with no refusals; hv5 05:22 device lost with the prepass probe
  as the only strand shader running. The prepass probe was drawn at the card's own depth under the native (strict)
  prepass depth test, so equal-depth fragments failed.
- Now card samples stay in the prepass; strand samples come from the G-buffer pass: the strand variant with colour and
  depth writes off after the native card draw (or the swapped strand draw itself). rt/hair_primary_probe.hlsl removed.
- Live check: hv6 order-1 windows, strand/card about 0.9-1.1 on the Dialogue1 close-up, swaps follow; hv5 survives.
