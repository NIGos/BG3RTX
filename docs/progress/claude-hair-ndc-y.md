## Strand rays: clip y negated (branch claude/hair-ndc-y, READY_FOR_LIVE, 2026-11-04)

- hv6 07:15: the sampled strand ray's card point (1.286, -0.255, 1.944) against the groom bounds centre (1.336, 0.408,
  1.884): x and z on the groom, y mirrored. The game's vertex shaders write gl_Position.y = -clip.y (rt/xfb_to_world.hlsl,
  the groom points' space); the strand G-buffer and velocity variants inverted vp from the Vulkan NDC without the
  negation, so every strand ray was mirrored vertically and missed.
- Fix: NDC y negated in both variants (test: hair_groom_test strand ray round trip). The pixel-record count uses the tag
  applied in its own recording (it counted with a newer publish's tag: 0 traced entries; C2's colour samples too).
- Live check: hv6 order 1, strand/card about 0.9-1.1, strand traces line with traced and strand entries > 0.
