## RTX Hair On bundle: add rt_hair_groom_nearest (live hs1 133243) (branch claude/hair-nearest-default, 2026-11-10)

- hs1 133243 (S8 still, v3 set, key on, VALID): 30 of 30 periods with 0 frame watchdog, 0 key watchdog, 0 dropouts and
  no "coverage low" switch; 2 "coverage stale" switches (tiny-hair item). Strand G-buffer GPU median 0.171 ms/frame
  (0.118..0.320) against 0.227 (0.097..0.307) in 125217: no measurable cost.
- Not reproduced: the three arrivals (joins 13:34:25.14-25.62) fell 0.6-1.1 s after the switch-on, inside the warm-up
  window (as 103633). The run shows no regression; the proof of the fix is the GPU case (2,567 holes to 0).
- rt_hair_groom_nearest=1 added to the "On" choice of exp_hair_groom_primary (code default 0, schema, settings_test
  pins, bg3rtx.ini comment). Compiled at pipeline creation: a game started with RTX Hair off keeps the old trace until a
  restart. settings_test 6271 checks pass; build.ps1 -Test. No shader or ABI change, variants delta 0.
