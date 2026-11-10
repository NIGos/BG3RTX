## hl1 161106: the late-arrival test did not reproduce the holes; the natural arrival did, with the nearest trace on (branch claude/hair-arrival-cause, offline, 2026-11-10)

- Disproved: predicted.json of hl1 ("arrival-a with the restart loop shows holes"). Holding 45252 for 301 frames and
  releasing it while the other keys swapped gave 0 frame watchdog trips and 0 dropouts (mask-hole pixels 936). The
  hold-one-groom reproduction is wrong; "the newcomer's strands block the others' rays" (5u) is not the mechanism.
- The holes came at the natural arrival in phase "lower" with rt_hair_groom_nearest=1: frame watchdog at G-buffer
  frames 1992, 1994, 1996, 1997 (0/5383), 1998; 31 dropouts (rays missed 10, rejected after the hit 20); the period's
  coverage line: nothing traced 20, traced then rejected 10, frame-wide 3 of 60. At frame 1992 the first newcomer
  (45252) is only "UV join pending": no groom row, no instance, nothing in the groom TLAS. The existing keys (0d51,
  0754, 51e8, 2e35) lost their strands before any newcomer strand existed. So the cause is not ray skipping and the
  nearest trace cannot fix it (nor did it).
- Q1 (was the nearest trace on?): the strand pipelines were created with the job's startup set (rt_hair_groom_nearest=1)
  and the header flag is set from the key every publish ("lower" sets 1), but no line proved it. Added: the strand
  G-buffer pipeline line prints "nearest-strand trace compiled in (specialization 1047) N", and a line whenever the
  header flag changes says which trace runs.
- Q2 (what changes for the existing keys at 1992): not decidable from the log and the code. The capture dropped
  nothing ([geo] RT capture: 0 draws dropped), slot jumps 0, header/BLAS mismatch 0, build camera unchanged, slot
  exchanges resolved as in every period. Missing single observable: for the dropped frame, whether the groom TLAS the
  strand G-buffer traced holds the instances the applied header's rows and key map describe (instance count and
  per-instance key of the TLAS built for that publish against the header's rows), logged per frame on mismatch.
- Q3 (why right after a settings read): the layer's settings read (util.cpp cfg_read_files) touches no hair or capture
  state, and the capture declined no draw before the arrival, so the game itself started issuing the three LOD 0 hair
  draws. In all five runs this came 0.06-0.6 s after the runner started a plan phase (hl1 20.2 s after scene ready,
  hs1 23.7 s), not at a fixed scene time. What the runner sends to the game at a phase start besides the ini (window
  messages, input) decides it; not decidable offline. A player's settings change writes only the ini.
- No fix landed (the cause is not established). Left: the observable above, then the fix at the cause; the hl1 job
  should gate the natural arrival in its first phase change instead of holding one groom.
