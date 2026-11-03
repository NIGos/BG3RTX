## Elastic demand headroom (branch cascade-elastic-headroom, READY_FOR_LIVE, 2026-11-03)
- Existing rt_capture_elastic: proactive cascade growth; RT/cascade reserve uses preset quarter reserve and measured positive demand steps over retirement + producer delay; falling cap cannot turn growth into shrink.
- Synthetic S8 envelope 206.18 -> 301.71 MiB fits before growth: 320 MiB static, 592 MiB after step, 320 MiB after 120 quiet samples; oscillation retains reserve.
- Three slots: static 960 MiB (+144 vs 272/slot, -321 vs fixed 427/slot); post-step 1776 MiB (+672 vs 368/slot); temporary old+new overlap up to 2736 MiB in this trace. RT fixed floor retained.
- Layer build and 2/2 capture CTests pass; synthetic step/oscillation/quiet/delay/cap tests. No game, commit, shader/ABI/key changes (variant delta 0). GPU cost unmeasured; layer restart required.
- Expected live: every su1 S8 Q/E/rotation window has cascade buffer full 0.0 and RT opaque buffer full 0.00. Unseen larger steps, stalled fences or refused allocations remain unbounded; not a live pass.
