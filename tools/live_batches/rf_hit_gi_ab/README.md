# rf_hit_gi tier (a) re-measure, S6 + S8 (live_batch rf-hit-gi-ab), NOT authorized

Two lean jobs (Adamantine Forge, Water Queens House; 184 s each) on Ultra as merged (+ Bounce, Hidden hits): diag-rf
(rf_hit_shade=1, rf_hit_gi=0), diag-gi1 (1), diag-gi2 (2: the tier (a) check from the screen's own GI), diag-gi1-box2
(ls_box=2), each with rf_stats; then cost-off-a / cost-on (rf_hit_shade + rf_hit_gi) / cost-off-b and three PNGs, the
exposure held. Why: the S6 tier (a) bias (1.090) and S8 (0.925) were measured before the game's lobes at hits
(RF_HIT_GI -> NATIVE_HIT_GAME_BRDF). Those ratios came from untraced diag windows, so the 385efd1 trace flaw did not touch
them. Predictions and their derivation: predicted.json (S6 1.064 / 1.17, S8 0.925 / 1.015). Deciding: the "rf hit shade
terms" and "rf hit gi" ratio per diag window against its range; cost of rf_hit_shade + rf_hit_gi from the cost windows.
Offline check: `powershell -File tools\live_batches\rf_hit_gi_ab\dryrun.ps1`.
