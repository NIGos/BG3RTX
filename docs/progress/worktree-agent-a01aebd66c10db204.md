## gi_far go / no-go on the full S8 pack: keep off (branch worktree-agent-a01aebd66c10db204, offline, 2026-11-01)

- Pack `replay-20261101-142819-1` (rc1, casters on, VT cache, lighting inputs 19 of 19). The caster instance has no
  geometry in the pack (tris 0, mask 0), so roofs still come from the asset DB: `PT_ROOFS=1` for the PT and the new
  `frame_replay --sun-trust` (`tools/pt_roofs.py --trust`, 159 padded volumes) for the gi_hit_sun pass.
- PT reference (roofs, real materials, env, sun LUT): far field 53.6% on screen beyond 1 m (visible 15.2, hidden 38.3),
  off screen 35.8% (not split), live domain 8.4%; sun / local 70 / 30%; k = 15.2. Native vs PT far + env:
  E x AO / ref 1.44 (tiles p10 0.90, median 1.75, p90 3.01); the preliminary 0.70 without roofs flips.
- Emulation of gi_far 6 m on the pack matches the live `GI far` line within 2% (visibility 0.833 vs 0.834, bounce
  0.0146 vs 0.0147). Predicted image change: 4 / 6 / 8 m -1.4 / -2.0 / -2.4% of the screen radiance (6 m: ambient
  -2.83%, bounce +0.88%), toward the reference (per-tile error 1.40 -> 1.31 -> 1.30, no overshoot).
- Live S8 measured +2.87% (diffuse +2.93%): even the bounce alone gives only +0.88%, so the live change is not
  explained by the GI far line. Energy gate fails. Kept off; no rule change indicated.
- Next: one live ON / OFF window with `ls_stats=1` and the GI images in the trace (giRaw, folded AO, gDiffuse before /
  after the composite) to find the stage downstream of giRaw; suspect the D-LS `ao_direct` give-back on the folded AO.
- Tools: `frame_replay` prints `GI hit sun` / `GI far`, accepts `--sun-trust`, and `--native-ambient` writes
  per-pixel PFMs (E, as lit, AO, PT total / live / env, k terms). CTests frame_replay_test and pt_reference_test pass.
