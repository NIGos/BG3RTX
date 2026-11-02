## Ultra: Far-field sun bounce 6 m (branch gi-far-ultra, READY_FOR_LIVE smoke_ultra, 2026-11-02)
- Gates (trace fix in): gf2 S8 031330 scene colour -2.01% vs committed -2.0% (band 1.32%), view vis ON/OFF 0.9471
  (predicted 0.947); camp 033251 -2.42% (band 2.29%, no prediction written; same sign, size as the S8 per-ray
  visibility drop scales: camp 0.816 -> 0.746 vs S8 0.889 -> 0.842, x1.5).
- Cost, GI trace pass: S8 1.09 -> 4.31 ms (+3.23), camp 3.26 -> 6.97 ms (+3.71); frame time +4.4 / +4.0 ms.
- Change: `exp_gi_far` leaves Experimental (Advanced, after Emission at ray hits), presets {0, 0, 0, 1}; schema
  regenerated; settings_test 934 checks pass. Not merged: smoke_ultra with it first.
- User check: S8 (Water Queens House) and camp, Ultra; Far-field sun bounce Off / 6 m: rooms slightly darker where the
  game's ambient was brighter than the traced far light; no light leaks under roofs.
