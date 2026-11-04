## Strand trace diagnostics and conditional hv6 burst gates (branch claude/hair-strand-diag, READY_FOR_LIVE, 2026-11-04)

- hv6 06:35 (bundle19): card samples real, strand samples 0 for every probed draw, and the counted frames held no
  strand entry at all: the strand variant's traces found nothing (or never ran). Cause not found offline.
- Diagnostics: the count shader reports the traced pixel entries of the counted frame and those with a strand; the
  strand G-buffer variant writes one ray of a sparse 64-pixel grid (origin, card distance, direction, committed, hit
  distance, groom, segment, tag, camera) to the header; one line every 300 frames
  ("[hair primary] strand traces in counted frame ..."). hv6's strand-window burst gates are required_if swapped.
