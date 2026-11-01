# Light conventions of the asset database (offline, 2026-11-01)

`python -m tools.assets.check_conventions [--json-dir D] [--csv F] [--level L --at X Y Z]` over
113 levels, 19,572 lights (12,041 point, 374 spot, 7,157 box); table `docs/evidence/light-conventions-20261101.csv`.

## 1. Distributions and flags
- **PreExpose** on 99.8% of point/spot lights (23 without) and on every box. **FlatFalloff** on
  79.6% of point/spot, 13.2% of boxes. Shadow: 2.2% of point/spot, 53.5% of boxes.
- Intensity: point/spot p5 2,000, median 12,000, p95 200,000; box p5 600, median 10,000.
  Radius: p5 1.5 m, median 7 m, p95 17 m, max 600 m; 27% are 10 m or more.
- Kelvin (9,616 lights): p5 3,000, median 6,400, p95 10,000 K. **3,450 Kelvin lights have a
  non-white Color**: ASSET_DB's "Kelvin lights keep Color = white" is wrong; keep `Color x Kelvin`.
- Gain: 1 (8,860), 0 (4,299), 0.5, 5, 0.15, 0.1 (not in the runtime intensity fit yet).
- Outliers: 1,164 lights 10x off their template's median intensity, 45 by radius; 176 enabled
  with intensity 0; brightest `Light_Dungeon_Fill_Blue` 3e7 (r 12.9 m).

## 2. Box lights
- Extents median 1.5 x 1.5 x 2 m, p95 depth 48.6 m. Attenuation function 0/1/2/3 =
  2,860/1,789/2,497/11 (bg3_boxes.hlsli curve index `m4 >> 24`: all in range).
- Under the plain metre mapping (start/end along z, side from each face; **not yet checked
  against a b29 record**): 329 boxes have overlapping z ramps (the shader's if/else makes the
  falloff jump at `ramp.x`), 1,862 side ramps never reach 1, 894 boxes peak below 0.5.
- **Box units are unverified.** A Forge lava box (I 16,000) x b13 0.00137 = 22, with no 1/pi,
  would light at luminance ~1. Check: dump `_m0`, `_m2`, `_m3` in the join CSV's P rows.
- **Why 16/25 and 16/58.** `layer/assetdb.cpp` counts box matches over every *valid* b29 slot
  (`n_proj`), not the froxel-listed ones (`pref` is only summed). b25 shows how many stale
  slots that can be (Grymforge: 1,182 valid, 39 listed). Both rates understate the listed
  set; fix: count `m_proj` for `pref[i]` slots only (done 2026-11-01: the log now reads
  "froxel-listed matched", over `n_proj_ref`). Other suspects: 685 boxes are
  CombinedLights, 32 in not-starting-loaded sub-levels, 15 moving; character rigs are not placed.

## 3. Lights inside roofed rooms (sun roof rule context)
- 8,261 / 19,572 lights (42.2%) sit in a roofed volume (room HasRoof/IsRoof, roof or covered
  floor); only 11 / 113 levels have such volumes. WLD_Main_A 55% (64 volumes), CTY_Main_A 81%
  (398), SCL_Main_A 46%, BGO_Main_A 36%. Forge camera outside; 104 / 182 nearby lights inside.

## 4. The Forge (S6) direct bias: L_b direct 0.0266 vs L_a diffuse 0.0110
- **Lights near the camera** (WLD_Main_A, 80 m): 151 points, all PreExpose, 141 FlatFalloff,
  0 shadowed (Underdark fill lights, r 8-12 m); 31 boxes, 1 shadowed. The screen log agrees
  (0.01 game-shadowed lights per pixel). **Atlas darkening cannot explain it.**
- **Same record, same formula.** `hd_native_incident` equals `bg3_light_eval`'s light factor
  (range, window^m4, m5 sign: flat `4/r^2` or `1/d^2`, 1/4pi or t^2/pi, `m3 < 0` x b13); boxes
  use `bg3_box_falloff` and `_m1` bit 0. **Units, b13, PreExpose and FlatFalloff cannot differ.**
- **Units sanity.** The game's point term on a 3 m grid around the camera: 0.048 before N.L,
  albedo and 1/pi; a missing b13 would be x731, not x2.4.
- **The x2.4 is not like for like.** L_b direct includes specular, L_a diffuse does not.
  Total L_b / L_a = 1.48; direct against (L_a minus L_b's ambient) is about 1.7.
- **BRDF ruled out.** FresnelBlend against the game's Burley/GGX, averaged over cosine L and V,
  roughness 0.3-1, F0 0.04/0.5: total ratio 0.93-1.13.
- **Light-side asymmetries left:** (1) **box lights at hits skip the game's AO-on-direct**:
  S6 has m0.y = 1; with `ls_box=0` the screen keeps box diffuse x AO (0.76 there), while
  the hit adds boxes outside `.native`, so `ao_direct=1` gives them no factor: **x1.32 on box
  diffuse**; S6 lists 2.4-2.6 projectors per pixel. **Fixed 2026-11-01:** while the screen's
  D-LS box pass does not run (Push::ls_flags bit 26 clear), rf_hit_shade returns the box term in
  `.native`, so it gets `native_hit_ao_factor(A)` as the game-visibility point/spot lights do
  (whole term, as for them; fixture 4c). With the box pass on, boxes keep D-LS's give-back. (2) Channel: Forge masks are 161 / 159 /
  191; a hit channel 0 where the G-buffer says 1-4 gives x1.46 on points. (3) Visibility:
  52.5% of hit light rays are lit, 35% on screen (another population).
- **Most likely cause:** the projector term at hits (no AO factor, unknown real weight), not
  a units convention. It explains at most x1.32 of its share; the rest is outside the lights.
- **Concrete check (one S6 run, `rf_stats=1`):** split tier (a) `L_b direct` into point/spot
  and box (two counters), histogram (hit channel, screen channel) at tier (a) hits, and toggle
  a key that multiplies the box term by `native_hit_ao_factor(A)`. The excess should fall by
  box diffuse share x 0.24; if not, look at visibility and material inputs next.
