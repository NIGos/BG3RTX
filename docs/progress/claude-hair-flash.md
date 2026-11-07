## The flash while anything moves: per-frame tags, LOD keep, counter fix (branch claude/hair-flash, offline, off, 2026-11-08)
- The 5h dropout counter judged the wrong G-buffer frame (the count reads the previous frame's markers): ~1000-1500
  false "no card marker" per period. Fixed: the frame the coverage evidence uses.
- Cause 1: two consecutive G-buffer frames sealed with one publish tag (current upgrade, then a different-grooms
  fallback): the second reuses the first's strand hits without tracing and the evidence turns ambiguous (coverage
  invalidated, cards for a while). Moving periods: ambiguous 24-28, invalidated 86-420, swap resumes 15-52; still: ~0.
  Key `rt_hair_primary_frame_tag` (live, off): the G-buffer frame is the tag.
- Cause 2: a LOD switch to a LOD still uploading drops the groom for 2-3 frames (cards); new slots start at LOD 0 then
  switch. Key `rt_hair_groom_lod_keep` (live, off): keep the built LOD until the new one is resident.
- Both CPU only. Live check: RTX_HAIR_PRIMARY 5i.
