## RTX Hair: final live hx1 194658 read, the bundle holds; coverage keep and card fill recommendations (branch claude/hair-final-report, docs only, 2026-11-14)

Table, gates and evidence: docs/RTX_HAIR_PRIMARY.md 6j. Image pairs: docs/hair_fill/. No code, no live, no new tool.

- Run: hx1 194658, S8, VALID, 25 of 25 phases, 229 s. Window occluded (372 of 372 checks): no frame time judged.
- Bundle alone (rt_hair_header_scene_pass 1, card fill 0, keep 0), 6 moving windows (2 dollies and 2 orbits of 180
  frames, 2 dollies of 120): 0 strands->cards of any cause, 0 frame watchdog trips, 0 frames with every draw on cards,
  own-bound fails 0, label taken by the decal pass 0 in all 32 key-on lines. Strand draws 97.4-99.1% of the hair draws;
  the rest are heads that enter the view (2-3 per window, cards for 10 frames). PASS.
- Dense samples moving 79-84% accepted / 3.9-5.6% no strand, still 80-87% / 3.4-4.8%: inside the still spread
  (78-90% / 3.0-6.5%). Control without the key: 59% / 27.4% and 70% / 14.4%.
- Control (in-base, out-base): decal pass took 179 of 216 and 129 of 184 labels; 7 and 1 frame watchdog trips (all 8
  lines of the run, frames 4974-4980 and 5157); 10 and 17 switches to cards; 29 and 23 of 181 frames all on cards.
- The 3 switches left with the key (pre-in, shot-on, shot-fill): all "coverage stale", all key 244b at 0-1 card
  pixels (a head off the screen for 28 frames). Unseen. Two fell into still windows (predicted 0 there: timing only).
- rt_hair_coverage_keep (in-keep, out-keep): 0 switches of any cause, 0 trips, "coverage stale" refusals 16-35 -> 0,
  card run of an entering head 10 -> 5 frames (3 "strike" + 2 "card depth not dropped" remain; predicted 0-1
  cards->strands, measured 3 and 2). The 0 switches alone do not separate it from the bundle (in-on, out-on also 0).
- Hitch frames: none ("by another scene pass" 0, second passes with their own header 0 in all 36 lines). The 6i
  per-recording header stays proven only by the CTest.
- Cost: strand G-buffer draws 0.13-0.20 ms zoomed out, 0.27-0.35 ms near. Keep 0.31 / 0.17 ms against 0.27 / 0.16;
  fill 0.15 / 0.35 against 0.20 / 0.32: no difference outside the spread.
- Card fill pairs (still-on 194931 | still-fill 195009, shot-on 194942 | shot-fill 195020; final image at 2560,
  3x nearest crops of 4-5 heads, |difference| x4, G-buffer albedo crops, full frames): the fill draws the card on
  5.2-5.6% of the card pixels zoomed out (151-161 of about 2900, about 13 pixels a head) and 4.5-5.0% near (883-1005 of
  about 19700), scattered between strands and along the outline. Final image: mean RGB inside the hair equal within 1
  of 255, no seam, no colour step; the visible differences are idle animation.
- Recommended: (1) rt_hair_coverage_keep into the On bundle. (2) Card fill stays an Advanced option (no visible gain,
  watchdog only reports with it, restart). (3) Remove rt_hair_stale_publish, rt_hair_row_stable, rt_hair_watchdog_draw,
  rt_hair_watchdog_one_key, rt_hair_swap_latch, rt_hair_coverage_min_pixels (low risk, all off in this run); the
  key-off header path later (medium: the only A/B of the fix). Keep the frame watchdog and the period lines.
- Left for "100%": styles without a groom file (S8: index counts 3399 and 7086, 2 draws a frame); hitch frames
  unproven in game; 5 card frames of an entering head with keep.
- User check: S8, RTX Hair On alone, zoom fast and orbit: heads in view keep strands. Fill decision: open
  docs/hair_fill/shot_heads_final_a.png, shot_heads_final_b.png, still_heads_final.png.
- Status: user decision pending on the card fill; coordinator decision on keep and the removals.
