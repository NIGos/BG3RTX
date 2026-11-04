## Coverage count reads the newest tag present (branch claude/hair-count-tags, READY_FOR_LIVE, 2026-11-04)

- hv7 09:48/09:57: strand pixels 0 for every draw although rays hit: the count (recorded after the G-buffer pass) ran
  on the GPU before the last G-buffer passes; the record held tag - 2 while the count looked for tag.
- The count shader now counts strand pixels per groom for the four newest tags; the layer takes the newest with
  entries and its G-buffer frame's keys (per-frame applied tag record). The strand-traces line logs the four totals.
