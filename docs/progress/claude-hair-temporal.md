## Temporal grain from the hair-ab7 bursts (branch claude/hair-temporal, offline, 2026-11-07)
- New tool tools/hair_groom/temporal_burst.py: per pan burst, inside the hair mask, temporal flicker, motion-compensated
  frame difference and spatial HF of the pre-RR colour and of the RR output (B10G11R11 / A2B10G10R10 raw decode).
- Tips off (live): main draw 0.921-0.933 -> 0.958-0.972; 210b and 35ce reach 0.93-0.98 and swap; f5c0 and 6e26 do not;
  hair share 0.847/0.849 -> 0.876/0.881. Prediction met for the main draw and hair share, partly for small draws.
- Clean frames (0-7): strands flicker less than cards before RR (0.455 vs 0.571) and after RR (motion-compensated
  difference 0.0146 vs 0.0199); RR output HF 0.029 vs 0.037 (strands softer, like skin at 0.029).
- Found: a one-frame strand dropout in the strands burst (frame 8: 4,689 hair pixels vs ~41,500, no camera cut);
  the RR output loses the hair for that frame and the hair difference stays 3.3x higher for 7 frames.
  The 220524 warm-up window showed the same bald frame. Cause not yet located (no dropout counter in the log).
