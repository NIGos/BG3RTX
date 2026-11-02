## rf_hit_shade S6 direct excess: the BRDF, not the boxes (branch rf-hit-box, READY_FOR_LIVE, 2026-11-01)
- Live m4p3 161852 (`rf hit shade boxes`): the boxes are 0.00508 of tier (a) L_b 0.0764 (6.6%), already at the
  game's visibility with the AO factor; point/spot are 0.0606 of the 0.0657 direct. The box path (channel, falloff,
  cookie, shadow strength, AO factor) matches f469db78. What differs for every light: the hit used the declared
  FresnelBlend, the screen the game's Burley x mix(1, 0.6622, a) + GGX/Smith-joint/Schlick.
- Fix (behind `rf_hit_gi`, define RF_HIT_GI -> NATIVE_HIT_GAME_BRDF): point/spot, box and sun at hits use
  `hd_game_scatter` (common/native_hit_direct.h), with the same domain checks and status codes. Left out: the
  screen-quad AA roughness term (a hit has no derivatives).
- Test: `box_light_test` section 8: hit box term == the game's box loop on 98k random records/surfaces (0 failed;
  2^-14 relative plus the GGX-peak and grazing N.L bounds). FresnelBlend / game energy over the same cases 0.92.
- S8 replay proxy (gi_hit_direct with the define, the same hits and lights): direct sum 36.34 -> 35.31 (-2.8%),
  supported set unchanged.
- Expected tier (a) L_b / L_a: S6 1.090 -> about 1.06 without the GI term, about 1.12-1.22 with rf_hit_gi
  (was 1.15-1.25). S8 0.925 -> unchanged (its direct is 0.0057 of 0.14), about 1.0-1.03 with rf_hit_gi.
  The remaining S6 excess is not in the lobe or the boxes: the next suspect is visibility (lit 71% of rf light rays
  vs 56% in the GI at S6).
- Gates: 91/91 CTests. rf_hit_shade_shader_test PENDING: stopped at 481/1464 (0 failed) when a live run started; rerun with no bg3.exe (cached).
