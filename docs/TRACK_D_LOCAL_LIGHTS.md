# Track D: D-LS ray-traced local-light shadows

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
Section 9 (including the box lights, 9.10 and on) keeps its original numbers.

## 9. D-LS: ray-traced local-light shadows (implemented)

Status (2026-10-26): **on by default** (`rt_local_shadows=1`, live; needs `rt=1`).
Code: `shaders/gi/rt_local_shadows.hlsl`, `shaders/rt/bg3_lights.hlsli` (the light list
and the game's per-light shading, D-EN1), `shaders/gi/rt_local_shadows_prep.hlsl`
(validation only), `core/gi.cpp` (snapshot, bindings, pass, timing, statistics).
Tools: `scripts/live_phases.py` (live setting phases during one auto-run),
`scripts/ab_compare.py` (A/B stills and crops from two captures).
Measured in the Steel Watch Foundry (the auto-run save), RTX 5090, 2560x1067.
Reused on the forward water (M5.4b `rt_water_ls_shadows`, docs/PLAN_M5_WATER.md): the water FS's set 2
b10/b11/b12/b26/b27/b28 have these structures, and `bg3_light_eval` / `bg3_atlas_shadow` reproduce its loops.

### 9.1 The light list (D-EN1)

At the first tiled-lighting dispatch the core snapshots, with `query_binding`, the
lighting's own set 1 b0 (flags), b13 (global light scale), b23 (froxel headers),
b24 (index list), b25 (light array), b39 (lighting constants), b40 (shadow views),
b41 (shadow atlas) and set 0 b2 (the froxel depth scale), and re-binds them read-only
to the D-LS pass (push-descriptor bindings 21-29). Every offset comes from the SPIR-V
`Offset`/`ArrayStride` decorations of `f469db78` (listed in `bg3_lights.hlsli`).
If any of them is missing, the pass is skipped and the log says so once (R8).

| What | Value in the foundry |
|---|---|
| Light array b25 | 4096 slots of 80 bytes (327680 B); shadow views b40: 4096 x 96 B |
| Froxels b23 / index list b24 | 397440 B (24840 froxels) / 6.36 MB |
| Shadow atlas b41 | 8192x8192 R16_UNORM, SHADER_READ_ONLY |
| Lights listed in the pixel's froxel (points + spots) | **5.61 per pixel** |
| Lights the game actually evaluates (channel, range, cone, N·L > 0) | **2.31 per pixel**; 89% of pixels get at least one |
| ... of which have a game shadow map (`m7` bit 0) | **0.00**: in this scene no point/spot light casts a shadow |
| Projector lights listed (b29, third loop of `f469db78`) | 1.02 per pixel (not handled, see 9.7) |
| `set1/b0 m0.y`, the AO factor on direct light (QUALITY Q3) | **0.5** in gameplay (1.0 on the loading screen); `m0.x = m0.w = 1` |
| `b13[0]`, the global light scale (lights with `m3 < 0`) | 0.000407 |

So Q3 is answered: BG3 multiplies direct local and sun light by `mix(1, AO, 0.5)`.
Our AO fold (RT AO / RT GI visibility into `b6.x`) therefore also darkens direct light
by half its occlusion. That is the game's own formula, and it is kept.

### 9.1a Water world-query candidate (2026-11-03)

`rt_water_hit_shade=2` queries the current frame's point/spot union by world
bounding sphere outside the froxel volume (REFLECTIONS_PLAN.md C). Unlisted b25
slots are stale, not an active full list (ASSET_DB.md: Grymforge 1,182 valid-looking,
39 listed). Membership/type is built once on GPU; nearest K=32, ties by slot id.
More than K overlapping spheres retain the native probe and increment device-local,
group-reduced `world truncated` counters. Sun/roof and global sky reuse native
sources; world boxes/local probes and never-listed lights remain unsupported.
Default off; cost and energy gates await an authorized S8 run. ABI unchanged.

### 9.2 What the pass does

Full resolution, after the lighting and **before** the GI composite (so next frame's
bounce source, `prevRadiance`, already contains the shadows), SSS and fog.

1. It rebuilds the surface exactly as `f469db78` does: `P = ray(uv)·depth`, the view
   normal rotated by `_m6` (not renormalised), F0 `mix(0.16·spec², base, metal)`, the
   diffuse colour, the light-channel bits, roughness `max(0.09, r)` with the game's
   specular AA (`sqrt(r² + min(var, 0.18))`; the 2x2 quad's normal differences are read
   from the neighbours instead of quad shuffles), N·V and the IGN of the atlas PCF.
2. It walks the pixel's froxel (`log2(depth·_m4)·_m21 + _m22`, tiles of `_m25`) through
   the point lights, then the spot lights, and recomputes each one's **unshadowed**
   diffuse and specular `c_i` with the game's terms: the range test, the
   `pow(sat(1 − x²), m4)` falloff with both bias models, the spot cone², `1/4π` (points)
   and `1/π` (spots), `b13[0]` for `m3 < 0`, GGX D, the approximate height-correlated
   Smith visibility, Schlick F, and renormalised Burley × `mix(1, 0.662, a)`.
3. For each light the game left **unshadowed** (`m7` bit 0 clear), it traces one ray
   (alpha-tested TLAS, `rt_occluded`) and adds the exact delta
   `out += c_i·(v_i − 1)·mix(1, AO, m0.y)` into b10 (diffuse) and b11 (specular, with the
   game's Lagarde specular occlusion in place of AO). Since `v_i ≤ 1`, a light can only
   lose what the game gave it. The result is clamped at 0 only against the
   transmittance, which the delta does not replicate (it is 1 indoors, see 9.3).
4. Non-finite guard on both writes (R5).

**Lights that have a game shadow map** (`ls_shadowed`, default 0 = *skip*): their
contribution is left exactly as the game computed it. Reason: the atlas is rendered
from the light and contains casters the TLAS does not have (culled walls, cut-away
roofs, everything off screen). Replacing it with the traced visibility could
*un*-shadow those, which is worse than the low-resolution penumbra it would fix.
- `ls_shadowed=1` replaces the atlas by the traced visibility (`c·(v − s)`, with `s`
  the game's own PCF term replicated exactly: tetrahedral face, 12-tap Vogel disc
  rotated by IGN, `textureGather` emulated with loads, camera-distance fade × `m8`).
- `ls_shadowed=2` takes `min(v, s)`.

Both are A/B options only. Neither could be judged here, because no light in the
foundry has a shadow map.

**Sampling (R1).**
- With RR (`rr=1`; `ls_soft=-1` follows it) each traced light gets one ray to a
  uniform point of its sphere by solid angle (radius `ls_source_radius`, 0.05 world
  units: BG3's lights have no source size, so this is an artistic value). White noise
  from `pcg3d(pixel, frame·1024 + light index)`. RR denoises the penumbra; nothing of
  ours filters it (R2).
- Without RR there is no denoiser, so the ray goes to the light centre (hard shadows,
  no noise).
- `ls_samples=1|2` instead traces 1-2 rays per pixel to lights chosen by RIS over the
  unshadowed luminance (unbiased; cheaper, noisier). It is an option, not the default,
  because 2.31 rays per pixel are affordable.

Rays start at `P + N·dist·rt_bias` and stop `ls_clear` (0.2) short of the light, so a
lamp's own housing does not shadow it completely:

| `ls_clear` | occluded light evaluations per pixel (of 2.31) |
|---|---|
| 0.05 | 1.00 |
| **0.2** (default) | **0.80** |
| 0.5 | 0.77 |

Most of the step from 0.05 to 0.2 is the fixture geometry around the light. Beyond
0.2 almost nothing changes, so the occlusion that remains is the scene's.

### 9.3 Validation of the recompute

`ls_debug=4` runs `rt_local_shadows_prep.hlsl` before the lighting. It sets the game's
AO to 0 (no ambient; direct light keeps `1 − m0.y` of itself, which the recompute
applies too) and its sun mask to 0. The game's diffuse + specular then hold only the
direct local lights, plus the terms the recompute does not model (projector lights,
the atmosphere's in-scatter). With `ls_stats=1` the core fits
`game = k·recomputed + r` per pixel, over the pixels whose froxel lists no projector
light (counters in `rtStats`, 120-frame windows):

| | AO 0 (`ls_debug=4`) |
|---|---|
| Fit over 1.20M lit pixels | **game = 1.07 × recomputed + 0.0134** |
| Mean recomputed / game | 0.0093 / 0.0234 |
| Game where no local light reaches (0.15M px) | 0.0181 |
| Recompute above the game (luminance) | 0.004% of the game's; 0.02% of pixels |

- The residual is an additive term of about the same size where no local light reaches
  at all (0.018) as under the lights (intercept 0.013).
- The recompute is never above the game's output. An error in a factor, the falloff or
  the BRDF would produce that somewhere.
- The slope of 1.07 is within what that spatially varying additive term can bias a
  least-squares fit by.
- The exact < 1% check of D-EN1 needs that residual removed too (the in-scatter
  replicated, or the projector lights added to the recompute). That is not done.
- `ls_debug=5` shows the recomputed light; `ls_debug=6` is the same check with AO = 1.

### 9.4 Results (A/B, same view, one run, live toggle)

`auto-run.ps1 -Traces 3` with `scripts/live_phases.py`: trace 1 on, trace 2 off (set live
right after trace 1), trace 3 `ls_debug=2` (the light removed, ×4); then `scripts/ab_compare.py`.

- [ls_foundry_off.png](img/ls_foundry_off.png) / [ls_foundry_on.png](img/ls_foundry_on.png):
  the presented frames (after RR, post and exposure), downscaled.
- [ls_foundry_removed.png](img/ls_foundry_removed.png): the light D-LS removes (×4,
  written over the lit diffuse, so it also passes through SSS and fog). This is where
  light previously leaked through geometry:
  - the floor behind the curved beam (right);
  - the beam's back face and the area above its lamp;
  - the walkway under the pipes and railings;
  - the machinery at the left;
  - the character's shadow wedge across the walkway, from the light above it.
- [ls_foundry_crops.png](img/ls_foundry_crops.png): three 1:1 crops of the 3840x1600
  frame, rows off / on / removed.

| Metric | Value |
|---|---|
| Light removed (unshadowed lights only) | **7.2% of the lit diffuse + specular luminance** (mean 0.0046 of 0.063) |
| Occluded evaluations | 0.80 of 2.31 per pixel (35%) |
| Mean luminance of the presented frame | −2.6% (exposure adapts partly); HDR compose output −2.3 to −2.7% |
| Non-finite values (every dumped image of all three traces) | **0** |

The render-resolution HDR images (compose, before RR) are not usable for a diff: the
per-frame jitter and the 1-spp noise of RR mode dominate it. The presented frames and
the debug view are the evidence.

Not done from R6:
- the docks, foliage and night-interior scenes. The `-Move` pan reached the docks once;
  there 0.07 lights are listed per pixel and none is evaluated, so the pass costs
  0.068 ms and changes nothing;
- FLIP against an accumulated reference;
- the temporal-stability metric.

### 9.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry, static |
|---|---|
| **Default: every unshadowed light, 1 ray each, soft (RR)** | **0.345 ms** (p10 0.342, p90 0.348) |
| Same, hard (`ls_soft=0`, the `rr=0` path) | 0.337 ms |
| RIS, 1 ray per pixel (`ls_samples=1`) | 0.219 ms |
| `-Validation`, default | 0.325-0.329 ms |
| Docks (no lights evaluated) / loading screen | 0.068 / 0.12 ms |
| `ls_stats=1` (wave-reduced atomics; measurement only) | 4-5 ms |

The budget (QUALITY R7) is 0.5 ms for local-light shadows: met. All rt passes together
(sun shadows + RT GI 0.26-0.28 ms, D-LS 0.35 ms) stay well inside the 2.5 ms Track D total.

These are the numbers when D-LS shipped. Later merges (per-class terms, ray origins, the
BVH split, per-light data) took it to 0.59-0.77 ms; 9.9 has the profile and the optimised
pass (0.39-0.42 ms in the foundry, same image).

### 9.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_local_shadows=0` against `1` with every mode toggled
  live in the lit foundry (`ls_stats`, debug views 1-6 including the validation
  pre-pass, `ls_samples=2` with `ls_shadowed=1`, `ls_shadowed=2`, live off and on):
  **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the
  headless `non-acquired-swapchain-image-used`). No core faults.

- The first on-run showed one new ID, `VUID-vkCmdDispatch-imageLayout-00344`. The
  validation pre-pass lived in the main shader, so the sun mask (binding 16) was
  statically used by the post pass while it was in SHADER_READ_ONLY. Fixed by moving
  the pre-pass into `rt_local_shadows_prep.hlsl`.
- `-Validation -Move`: the pan left the foundry for the docks (a large change of
  geometry and an outdoor view). No faults, no non-finite values, IDs as above.
- Missing light-list bindings, no TLAS or a stale one (more than one frame old): the
  pass does not run, and the game's lighting is untouched. It only ever removes light
  the game added this frame.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-LS on: DLSS-G status 0x0, 2 presents per
  rendered frame, 0 Streamline API errors; the pass costs the same (0.331 ms).

### 9.7 What does not work / next steps

- **Projector lights** (b29, about 1 per pixel here) are not shadowed. Their loop has
  its own falloff and cookie terms; adding them follows the same pattern.
- **Shadowed lights** are skipped by default. `ls_shadowed=1/2` are untested in a scene
  that has them (a torch-lit interior with shadow-casting lights is needed, R6).
- **Art intent.** Every unshadowed light is shadowed, including fill lights that may be
  meant to leak and the light that follows the active character (its wedge-shaped
  shadow on the floor). `ls_min_radius` and `ls_strength` exist as controls. A
  per-channel exclusion was not needed here. **Resolved for the character light
  in 9.8.**
- **Off-screen casters** are missing from the TLAS, so leaks through geometry the camera
  does not see remain (cascade-caster capture, 6.7 item 2, and D-EN3 retention).
- **Exactness.** The transmittance and in-scatter are not replicated (they are why 9.3
  stops at a fit), and the delta is clamped at 0 for that reason.
- **Light source radius.** BG3 gives none; 0.05 is a guess, to tune by eye.
  **Now per fixture class (9.8)**; still not a measured size.
- **`rr=0`**: hard shadows only (no SIGMA).

Settings (`bg3rtx.ini`, all live): `rt_local_shadows`, `ls_shadowed`, `ls_source_radius`,
`ls_soft`, `ls_clear`, `ls_min_radius`, `ls_samples`, `ls_strength`, `ls_debug`
(1 lights per pixel, 2 light removed ×4, 3 traced visibility, 4/5/6 validation),
`ls_stats`. The log reports `ls: first frame ...` (the bindings),
`ls: local-light shadow pass GPU median ...` and, with `ls_stats=1`, `ls stats` and
`ls validation fit`.

### 9.8 Per-light data: the Script Extender bridge and the asset database (2026-10-26)

Status: **on by default** (`ls_lights=1`, `ls_owners=15`, live).

Code:
- `core/ls_lights.cpp` (upload);
- `shaders/gi/rt_local_shadows_match.hlsl` (join);
- `rt_local_shadows.hlsl` (use);
- the data comes from `Bg3AssetDbApi` v2 (`layer/assetdb.cpp`, `layer/sebridge.cpp`;
  docs/SE_BRIDGE.md).

**What it does.**
1. **Upload.** Each frame the core uploads the runtime lights within
   `ls_lights_radius` (80 m) of the camera into a host-visible ring: up to 1,024
   records of 48 bytes. These are the bridge's lights merged with the database's placed
   lights, or the database alone without the bridge.
   - Each record holds the absolute position, the radius, the source radius, a match
     tolerance, the flags, owner, type and channel.
   - The list is re-queried on each new export, else every 0.25 s (was every 8 frames).
   - **Camera position.** The join pass writes it (set 0 b2 `_m6`) back into the ring.
     The core reads it kRing frames later to centre the next query. Before the first
     readback, the controlled character's position from the bridge is used.
2. **Join pass** (before D-LS, inside its timestamps). One thread per light array
   slot: 4096 threads, with the records staged through group-shared memory.
   - A slot matches the nearest record within its tolerance whose radius agrees with
     1/`m0.w`: 0.5 mm + 4 relative ulp for placed lights, 2% + 1 cm (not derived, logged)
     for moving ones. *Updated 2026-10-27, derivations in SE_BRIDGE.md "Join tolerances".*
   - Tolerance (2026-10-27): √3 × (4 ulp at the position + 0.05 mm) + `MovementAmount` for
     placed lights; that plus `ls_dyn_speed` (8 m/s) × the measured export age (≈ 3-4 m)
     for character lights and every other light the bridge reports. (Before: 5 cm, 4 m and
     `ls_dyn_tol` 1.5 m, replaced; the text below describes the lights it applies to)
     (items, and visual lights whose owner was not found, such as the VFX lights of
     carried weapons: `VFX_LightSources_WPN_DippedInFire`, owner 0). They move between
     the 4 Hz exports. Until 2026-10-27 items had 0.5 m and the rest 5 cm, so a carried
     weapon's light went unmatched while its carrier walked and lost its fixture radius
     (the ray's end clearance at the hand).
   - The result goes to a 4096×1 RGBA32UI image: source radius, flags, owner | type,
     and the record index.
3. **D-LS** reads its slot for every light it would trace:
   - the light's **source radius** replaces `ls_source_radius` for its soft-shadow
     sphere (the fixture class, SE_BRIDGE.md "Source radius");
   - a light whose **owner** bit is clear in `ls_owners` is left exactly as the game lit
     it;
   - unmatched lights keep `ls_source_radius` and are traced as before.

**Art intent: the character light (the 9.7 question).**
- **What the light is.** The light that follows the active character is the darkvision
  pair:
  - `Light_Player_Darkvision_Spot_Drow`: spot, radius 24, intensity 4000;
  - `Light_Player_Darkvision_Point_Default`: point, radius 18, intensity 0, `FillLight`.

  Both sit 2 m above the controlled character.
- **The game's own flags.** Their templates set `Shadow = false` and
  `CastShadow = false`. The game never shadows them.
- **What it is for.** It is a readability light: it keeps the controlled character and
  the ground around them visible in dark areas.
- **What tracing did.** Traced, its own carrier occludes it. The character's head and
  shoulders shadow the body, and a wedge falls on the floor. That darkens the character
  the light exists to show.
- **Decision.**
  - Character-owned lights are **not shadowed** by default: `ls_owners=15` (unknown,
    placed, item, scenery).
  - This follows the game's own shadow-caster flag for this light.
  - `ls_owners=31` traces them as before.
  - Placed, item and scenery lights are still shadowed even though most of them also
    have `Shadow = false`. Their leaks through walls are what D-LS exists to fix (9.4),
    and the game's flag there means "no shadow map", not "meant to leak".

A/B, same view, one run, live switch (`scripts/live_phases.py`, `scripts/ab_compare.py`):
- [sebridge_charlight_crop.png](img/sebridge_charlight_crop.png): 1:1 crop of the
  presented frame. Left is `ls_owners=31` (character light traced), middle the default,
  right |diff| ×4.
  - Traced, the character is visibly darker: the light above them is blocked by their
    own head and hat.
  - The default restores the game's look of the character.
  - The full frames are [shadowed](img/sebridge_charlight_shadowed.png) and
    [default](img/sebridge_charlight_default.png).
  - Mean luminance +0.15%; 5.8% of pixels change by more than 2/255, almost all of them
    on the character.
- [sebridge_owner_view.png](img/sebridge_owner_view.png): `ls_debug=9`. The evaluated
  light, luminance-weighted, coloured by owner:
  - white: placed lights;
  - orange: the glasshouse lamp items in the background right;
  - magenta: around the character (the darkvision light);
  - red: unmatched (none here).
- [sebridge_radius_crop.png](img/sebridge_radius_crop.png): per-light data off
  (`ls_lights=0`, 0.05 m for every light) against on, at the machinery on the left.
  - The changes are small penumbra differences on rims lit by fill lights (0.6 m
    instead of 0.05 m).
  - Over the frame, 8.3% of pixels change by more than 2/255, and the character
    accounts for most of it.

**Numbers** (foundry, `ls_stats=1`, 120-frame windows):

| | Value |
|---|---|
| Listed point/spot lights covered (join check) | database 26/40 (65%) → bridge + database 40/40 (100%) |
| D-LS light evaluations with per-light data | 100% matched (2.49 per px) |
| Evaluations not traced by owner (the character light) | 16.6%: it lights most of the walkway |
| Light removed by D-LS, all owners / default | 16.5% / 15.9% of the lit luminance |
| Non-finite values (every dumped image, four traces) | 0 |

**Cost.** GPU timestamps around the join pass and D-LS together, medians of 300 frames,
foundry, static, one run, live phases:

| Configuration | ms |
|---|---|
| per-light data off (`ls_lights=0`: the 9.5 path) | 0.587-0.590 |
| **bridge + database (default)** | **0.628** |
| database only (`se_bridge=0`) | 0.68 |

- **Bridge + database: +0.04 ms.** That covers the join pass, the larger penumbrae of
  fill lights, and the character light no longer being traced.
- **Database only: +0.09 ms.** The character light is still traced, and fill lights get
  the larger radius.
- **The base itself has risen** from 9.5's 0.345 ms to 0.59 ms on this branch (after
  the RR and BVH-split merges). That is not caused by this change. See 9.9 for where the
  time went and the optimised pass.

**Validation.**
- `-Validation` on the foundry, against a baseline run of main without the bridge, gives
  the same 7 message IDs (`10788`, `09600`, `StorageImage-FormatMismatch`,
  `OutputNotWritten`, `non-acquired-swapchain-image-used`, `CopyBuffer-00118`, `08737`).
  No new ID.
  - After merging main's RR post chain, a re-run adds `vkDestroyImage-image-01000`.
  - It comes with the `[rrpost] the game destroys an image our post steps read` log
    line, so it is the RR branch's, not this change's. That was not confirmed with a
    validation run of main alone.
- Without the database and the bridge (no API v2, `ls_lights=0`, or a failed
  allocation), the join pass does not run, and D-LS is unchanged from 9.2.

**Not done.**
- Projector (box) lights: the character's rim and fill lights are boxes, and D-LS still
  ignores box lights.
- A measured fixture size from the TLAS.
- Other scenes with the bridge.

### 9.9 Cost: where the time went, and the optimised pass (2026-10-26)

Status: **on by default**, no setting to choose. The output is **bit-identical** to the
pass before (proof below). Code:
- `shaders/gi/rt_local_shadows.hlsl`: the default-settings variant (`LS_FULL=0`) in three
  phases, and the old single loop as `LS_FULL=1` (`rt_local_shadows_full.hlsl`), which
  `core/gi.cpp` runs only for `ls_samples`, `ls_shadowed`, `ls_debug` or `ls_stats`;
- `shaders/gi/rt_local_shadows_match.hlsl`: the join keeps unchanged slots;
- A/B tooling: `shaders/gi/ls_ref/` (the pass of `f5e9eab`, frozen by
  `tools/lsref/make_ls_ref.py`), `rt_local_shadows_ref.hlsl` (`ls_impl=0` runs it),
  `rt_local_shadows_check.hlsl` (`ls_check=1/2`), `tools/pipeline_stats.py` (driver
  register counts, no game needed).

All numbers: RTX 5090, 2560x1067, GPU timestamps around the join and the shadow pass
(the core now logs the join separately), medians of 300 frames, `-Background` runs with
live setting phases (`scripts/live_phases.py`), the first window after each switch
dropped. The GPU was shared with the user's desktop (another game at times), and the
foundry ran at 24 fps with `rt_bvh_split=1`: absolute values drift by about ±10% between
runs (the old pass measured 0.55-0.77 ms in the same save), so every comparison below is
within one run.

**Where the time went (old pass, foundry, one run; each switch from the default; shared GPU):**

| Configuration | ms | vs default |
|---|---|---|
| default | 0.77 | |
| no rays at all (`ls_min_radius=1e5`) | 0.37 | rays −0.40 |
| `rt_origin=0` | 0.70 | −0.065 |
| `ao_direct=0` (no sun recompute, no AO give-back) | 0.70 | −0.07 |
| `rt_bvh_split=0` (the frame then ran at 130 fps) | 0.69 | −0.08 |
| `ls_soft=0` | 0.73 | −0.035 |
| `ls_lights=0` (no join; the character light traced again) | 0.77 | 0.00 (join −0.011, character light +0.01..0.07) |
| `mat_classes=0` | 0.77 | **0.00** |
| `ls_owners=31` (character light traced) | 0.84 | +0.07 |
| `ls_samples=1` | 0.57 | |
| `ls_lights`, `mat_classes`, `rt_origin`, `ao_direct` all 0 | 0.64-0.66 | −0.12 |
| ... and `rt_bvh_split=0` | 0.59 | |
| `rt_alpha_test=0` (another run, default 0.59) | 0.535 | −0.06 |

- **The per-feature switches explain only part of it.** `mat_classes=0` costs nothing to
  turn off, although the per-class terms were the largest audit change: the price of
  the per-class code is paid in **registers**, whether a class is used or not.
- **Registers** (`tools/pipeline_stats.py`, the driver's own count): the old pass used
  **252 registers and 16 bytes of local memory** per thread, so 8 warps per SM (of 48).
  The ray queries, the alpha test at their candidates and the generic per-class BRDF
  were all live in one loop. A pure ray pass (`rt_shadow.hlsl`) needs 72.
- The rays themselves (about 0.25 ms after the optimisation) did not get cheaper: 2.08
  per pixel, the ray origins (`rt_origin`) and the BVH split's instance lookups at alpha
  candidates make them longer, and exactness forbids shortening them.

**What changed (all exact):**
1. **Two variants.** Every debug view, statistic, the RIS reservoirs and the
   shadow-map modes live only in `LS_FULL=1`. Nothing but the four sums the output needs
   is live in the default variant's loops.
2. **Three phases per 32 list entries** instead of one loop:
   1. *Which entries are traced*: the trace conditions first (no game shadow map,
      `ls_min_radius`, owner from the join), then `bg3_light_eval_mat`'s own acceptance
      tests (channel, range, cone, N·L or the class's transmission/hair test), from the
      few surface terms they need. The BRDF is no longer evaluated for the lights that
      are not traced (the character light alone was 16.6% of the evaluations).
   2. *The rays*, the k-th traced light of every lane together, with only the ray
      inputs live.
   3. *The rest of the surface* (reloaded at an index the compiler cannot prove equal to
      the pixel, so it is not kept across the rays), each traced light's exact
      unshadowed diffuse and specular, and the sums in list order with the old
      expressions.

   Result: **96 registers, no local memory, 21 warps per SM.**
3. **The sun is not recomputed where it cannot matter.** With `ao_direct=1` it only
   enters as `(1 − fdif)·sun`; where both AO factors are exactly 1 that is 0. Where the
   sun mask is 0 the cloud-shadow lookups are skipped (0 × a finite factor).
4. **The specular-AA neighbours**: the pixel's own normal is reused (2 normal loads instead
   of 4; the clamp at the odd last row behaves as before). `gAux` is read only in mode 2.
5. **The join keeps unchanged slots.** Each slot caches the inputs of its result (absolute
   position and radius bits, and a generation the core bumps when the uploaded records
   change); a group whose slots are all unchanged skips the search. 0.011 → 0.005-0.007 ms.

**Tried and dropped (measured, not kept):**
- Rays to the wave's union of list positions (rays to the same light together): +0.007 ms.
- The rays of the first 32 entries as their own dispatch (72 registers there): +0.07 ms;
  the second pass reads the G-buffer and the list again.
- No ray for lights of colour 0: exact, but no such light is traced here; no change.
- Skipping rays to lights whose unshadowed term stays below 1/256 or 1/64 of the
  pixel's (not exact): no measurable gain, and the extra evaluation raised the default
  variant to 125 registers (+0.03 ms).

**Not tried, or not applicable:**
- Wave-scalarised light loops and packed per-light caches. A wave's pixels mostly share one
  froxel list, the light records are read from L1, and the no-ray time is now 0.14 ms in
  all; the registers were the limit, not these loads.
- Ray flags: `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` was already set, and `tmax`
  already stops `max(R, ls_clear)` short of the light. Back-face culling would change the
  image (back faces occlude too).

**Result (foundry, same runs, new / old; shared GPU):**

| Run | new | old (`ls_impl=0`) |
|---|---|---|
| A/B 2 (4 + 4 alternating phases) | 0.388-0.401 | 0.566-0.595 |
| A/B 6 | 0.409-0.417 | 0.59-0.65 |
| A/B 6, `rt_bvh_split=0` | 0.377 | 0.563 |
| no rays (`ls_min_radius=1e5`) | 0.143 | 0.372 |
| join alone | 0.005-0.007 | 0.011-0.013 |

- **−0.18 to −0.23 ms (−31 to −37%)** at the same image.
- **The 0.35 ms target is not reached** under these conditions: 0.39-0.42 ms against an
  old pass of 0.57-0.65 ms in the same runs. Scaled to the 0.59-0.63 ms the old pass logged
  when this work started, that is about 0.40 ms. The remaining time is the rays
  (≈0.25 ms) and the G-buffer traffic of a full-resolution pass (≈0.14 ms with no ray at
  all). Reaching 0.35 needs fewer or shorter rays, which changes the image (for example
  `ls_samples=1`, or a lower resolution for the visibility).
- Under the 0.5 ms budget (QUALITY R7) again.
- Per feature, new pass (A/B 6): `rt_origin` +0.045, `ao_direct` +0.026, `rt_alpha_test`
  +0.015, `ls_soft` +0.012, `mat_classes` 0, the join + owner skip −0.04 (`ls_lights=0`
  traces the character light), `ls_owners=31` +0.056.

**Other scenes** (shared GPU; `-Save`, same tooling: a check phase, then new and old alternating,
three phases each):

| Scene | new | old | change | `ls_check=1` |
|---|---|---|---|---|
| Steel Watch Foundry (default save) | 0.39-0.42 | 0.57-0.65 | −31 to −37% | 0 px differ |
| Grymforge (`Grymforge - 32h 59m`, torches and lava) | 0.71-0.73 | 0.97-0.98 | −26% | 0 px differ (2.29·10⁹ px) |
| Water Queens House | 0.114 | 0.169 | −33% | 0 px differ (6.23·10⁹ px) |

Grymforge is above the 0.5 ms budget in both: it has many more traced lights (torches
along every wall). That is a ray count, not a regression of this change.

**Image equality.**
- `ls_check=1` runs the frozen old pass and the live one on the same pixel, frame and
  inputs in one dispatch, compares the float outputs bit for bit, and logs the counts per
  120 frames (`ls check`). It also searches every join slot and compares the kept results.
- Foundry, default settings: **0 pixels differing in any bit** over every window of every
  run since the last change (for example 2.62·10⁹ pixel evaluations in A/B 3, 1.97·10⁹ in
  A/B 6), and **0 kept join slots differing** from a new search.
- Also 0 with `ls_owners=31`, `ao_direct=2`, `rt_origin=0`, `ls_soft=0`,
  `mat_classes=0`, and for the `LS_FULL` variant with `ls_samples=1` and `ls_debug=2`.
- `ls_check=2` compares the old pass with itself (a second evaluation the compiler
  cannot merge): 0 over 16 windows. So the inputs do not change under the pass, and 0
  means equal.
- The `LS_FULL` check showed one window with differences in three runs, each time the
  window in which a setting switched variant (`ls_samples` or `ls_check`); the following
  windows are 0.
- A first version was not exact: it wrote the `ao_direct=1` give-back as
  `dir·(1 − fdif)` with a constant 1, and 2.1% of pixels differed in the last bits
  (17 px per 120 frames above 1e-5 relative). Keeping the old expression
  `dir·(td_f − fdif)` made it exact. The check is what found it.

**Validation layer and robustness.**
- Baseline: `-Validation` on `f5e9eab` (the commit before this work), foundry. 8 message
  IDs, all seen before: `10788`, `09600`, `StorageImage-FormatMismatch`,
  `OutputNotWritten`, `non-acquired-swapchain-image-used`, `vkDestroyImage-image-01000`,
  `CopyBuffer-00118`, `08737`.
- `-Validation` with this work, after merging main (`c604311`), foundry. Live phases:
  check, self-check, the `LS_FULL` check with `ls_samples=2 ls_shadowed=1`,
  `ls_debug=4` with `ls_stats=1`, the old pass, `ls_lights=0`, pass off and on.
  - The same 8 IDs, plus **`VUID-vkCmdDispatch-format-07753`**.
  - Cause: when the join does not run (`ls_lights=0`, or no per-light data), binding 46
    was given the RGBA16F `gAux` view as a placeholder. The shaders declare it RGBA32UI
    and use it statically.
  - This is older than this work: the 9.8 code binds the same placeholder. The baseline
    never turned `ls_lights` off, so it did not show the ID.
  - Fixed: a 1×1 RGBA32UI placeholder (`g_ls_nodata`).
  - Not re-confirmed: two later validation runs stayed in the main menu. The save did not
    load under the validation layer with `-Save`, and the newest save had changed under
    `-continueGame`. The menu-only run shows the 8 baseline IDs and no 07753.
- In that run, `ls_check=1` still found **0 differing pixels** after the merge with main
  (6.55·10⁸ px), as did the `LS_FULL` check (9.8·10⁸ px). Kept join slots: 0 differ.
- `-Move` (pan from the foundry): 2 traces in motion, **0 non-finite values** in 183
  dumps. No `nvlddmkm` event during the run.
  - An earlier `-Move` attempt ended with the game exiting 5 s after its first trace,
    with `nvlddmkm` 153 events. One A/B run also ended early that way.
  - `nvlddmkm` 153 pairs are logged on this machine at almost every game exit, including
    runs of the baseline code and other branches, so the cause was not identified.
  - The repeat `-Move` run was clean.

Settings (all live): `ls_impl` (1 the optimised pass, 0 the frozen old one), `ls_check`
(0 off, 1 old against new, 2 old against itself; measurement only, about 1.5 ms).

### 9.10 Who casts and who receives (audit 2026-10-27)

Question: in the on/off compares (`captures/compare`), characters and the lights they carry
seem not to cast or receive RT shadows. Findings (offline):
- **Casters.** Every instance has mask 0xFF (`bvhsplit.cpp` 893/918/1189, `rtscene.cpp` 746)
  and every ray traces 0xFF (`rt_common.hlsli`), in both `rt_bvh_split` modes. The TLAS is the
  big depth prepass (`geocapture.cpp` 1108; passes under 64 draws are skipped, 1382), direct
  triangle-list draws only (0 dropped in the logs). Skinned characters are in it: Campsite
  `captures/20261027-062830` has 14 draws of the skinned VS `1cf36b97` in the prepass (82
  class-B draws in the G-buffer, 14-23 in each cascade). Not in it: forward/transparent
  geometry (water, glass, particles, VFX flames), decals, off-screen and cut-away geometry.
  No class is excluded as a caster, and no game "does not cast" flag is applied either.
- **Sun.** Characters cast traced sun shadows (Lower City compare, the NPC on the sand: a
  crisp traced silhouette inside the game's soft one). Outside `rt_sun_replace`'s trusted
  pixels the fold is `min(game, traced)` (`rt_shadow.hlsl` 168), and the game's cascades
  already hold the characters, so on/off differ only at the shadow's edges.
- **Local lights.** Not traced, by rule: lights with a game shadow map (`ls_shadowed=0`,
  `rt_local_shadows.hlsl` 212), owner "character" (`ls_owners=15`, 218: the darkvision pair,
  which the game flags `Shadow = CastShadow = false`, and every `Light_Player_*` template,
  including world-fixed `Light_Player_Fill_*` lights; `BootstrapClient.lua` 134), and all
  box/projector lights (the characters' rim and fill rigs). In dark interiors (Adamantine
  Forge) these are most of the light on and around the party, hence no character shadows.
  Carried weapon lights (`VFX_..._WPN_DippedInFire`, owner 0) are traced; their join tolerance
  was the defect fixed above.
- **Receivers.** The sun fold edits the mask b7 and D-LS edits b10/b11, both read only by
  the deferred lighting. Every G-buffer class receives except unlit (hair is deferred, class
  5). Forward-rendered surfaces (water, glass, particles, VFX) receive neither: they sample
  the game's cascades themselves and get no D-LS.

**Policy (R0, coordinator 2026-10-27), implemented:**
- **The owner is no reason to skip a light** (`ls_owners=31`). The only skip is art intent
  from the game's own flags (`ls_noshadow`, `rt_local_shadows.hlsl` `ls_skip_light`).
  - Data (asset DB, 19,572 placed lights): **98% of point lights and 70% of spots have
    neither `Shadow` nor `CastShadow`** (only 0.8% / 0.4% of all lights set `CastShadow`).
    So "skip whenever both are false" (`ls_noshadow=2`) would stop tracing almost every
    placed lamp and bring back the leaks D-LS fixes (9.4); in BG3 the flag means "no shadow
    map", not "meant to leak". `Light_Player_Fill_*` also have both false.
  - Default `ls_noshadow=1`: a light is skipped only when both flags are false **and** it is
    the darkvision light that follows the active character (`BG3_RTL_PLAYER_LIGHT`, now set
    for `Light_Player_Darkvision_*` only). `ls_noshadow=0` traces it too.
  - `Light_Player_Fill_*` are no longer character lights: `sebridge.cpp` re-tags them on
    parse (owner unknown, no player flag), so this works with the installed v1 Lua. The Lua
    now classifies them like any other visual light (`BootstrapClient.lua` 134); the
    reinstall only refines their owner (item/scenery instead of unknown), it is not needed
    for the behaviour.
- **Lights with a game shadow map** (`ls_shadowed=3`, default; the High and Ultra presets):
  the traced visibility replaces the atlas term where the segment surface → light lies in
  the view frustum (the light projects on screen; the frustum is convex and the surface is
  on screen, so every opaque caster on the segment was drawn and is in the TLAS). Elsewhere
  `min(traced, atlas)`: the atlas has the off-screen casters. Not detectable: casters in the
  atlas but not in our prepass (forward/transparent geometry, if the game renders them into
  light shadow maps); they would be lost on screen. `ls_shadowed=1` replaces everywhere
  (Full+), 2 is `min` everywhere, 0 the game's map. The default variant (`LS_FULL=0`) now
  handles every mode, so mode 3 does not fall back to the 252-register pass. Hair keeps the
  atlas term (its self-shadow model for shadowed lights is not replicated). Note: `ls_check`
  / `ls_impl=0` compare against the frozen reference, which knows modes 0-2 only.
  - Cost. Budget: Ultra at 3840x1600 on the 5090 needs a base frame of ≥ 65 fps (15.4 ms)
    under FG 2x to 120; QUALITY R7 gives D-LS 0.5 ms. Rule: keep 3 on High/Ultra if it adds
    ≤ 0.15 ms over 0 and the pass stays ≤ 0.5 ms.
  - **Measured (session jobs `casters-S6/S8-*`, 2026-10-27, medians of 300 frames):**

    | Scene | `ls_shadowed=0` | `=3` | lights with a game shadow map |
    |---|---|---|---|
    | S6 Adamantine Forge | 0.372 ms | 0.375 ms | not measured (the stats job hit the open nv153 stall) |
    | S8 Water Queens House | 0.120 ms | 0.120 ms | 0.00 of 0.41 evaluations per px |

    | S5 Grymforge | 0.629 ms | **0.690 ms** (mode 1: 0.688) | 0.34 of 4.34 evaluations per px (7.8%) |

    S6 and S8 have no shadow-mapped lights in view. **Grymforge** (`32-casters-S5-*`, one
    session, no faults): replacing the 0.34 shadow-mapped evaluations per px costs
    **+0.061 ms** (traced 3.83 per px, occluded 1.13; light removed 18.0% of the lit
    luminance), within the +0.15 ms rule, and the on-screen test of mode 3 is free
    (mode 1 = 0.688 ms). **Default kept: `ls_shadowed=3` on High and Ultra.** The pass
    itself is over its 0.5 ms budget in Grymforge in every mode (0.63 ms at mode 0, the
    D-LS performance task of STATUS). The traces of the three modes cannot be compared
    frame to frame: their mean luminance swings 1772 / 419 / 77 nits with large bright
    VFX blobs in all three (mode 0 included), so a dedicated in-frame A/B (as `rt_debug=7`
    for the sun) is needed to judge the replaced shadows' look.
- **Other results of the same jobs.**
  - Coverage (S8, `rt_debug=3`): match 99.23%, miss 0.069%, in front 0.072%, behind 0.633%
    of which 0.629% "through vt cutouts": the stochastic alpha test at blurred mips letting
    primary rays through the edges of ferns and ivy (the debug frame shows them only there).
    The characters are matched. Missing geometry is ≤ 0.07%.
  - Skipped by art intent (`ls_noshadow=1`, owners 0x1F): 11.4% of light evaluations in S6,
    86% in S8, 11.8% in S5. That is the darkvision pair; in S8 most of it is the `Point_Default` half,
    which has intensity 0 (it covers the view at r = 18 and adds nothing). The owner view
    (`ls_debug=9`) shows the party's floor in S8 lit by an owner-unknown visual light (the
    carried fire weapon), which is traced; little magenta (character) light anywhere.
  - Join: unmatched 0.0% with `ls_dyn_tol` 0.05 and 1.5 alike: the carriers stood still in
    both sessions. The benefit needs a moving carrier (`-Move`), not yet run.
  - Non-finite values: 0 in S8; in S6 2.6-4.4k in every trace and mode, all in one D32F
    depth image of pass `2a6a525c` (the game's), none in an image of ours.

**Box (projector) lights: the gap and the plan (not implemented).** They are the third loop
of `f469db78` (set 1 b29, struct `_53`, about 128 bytes; lines 735-930 of
`captures/20261026-165848/shaders/f469db7865d7c524.glsl`). A box light is a **directional
light inside an oriented box**:
- `uvw = _m6 · (P, 1)` (row-major 4x3, world → box), lit only for `|uvw − 0.5| < 0.5`;
- direction to the light is the constant `_m9` (N·L = `dot(N, _m9)`, H from `V + _m9`);
- falloff: along z, ramps below `_m2.x` (× `_m2.z`) and above `_m2.y` (× `_m2.w`); across x/y,
  `(0.5 − |uv − 0.5|) · _m3` below 1; each ramp picks one of four curves by `_m4 >> 24`
  (linear, square, smoothstep, smootherstep); `_m4 & 0xFFFFFF` is the channel mask;
- colour `_m0`, optionally × a cookie texture `_m1 >> 1` (index − 1) at `uv`; `_m1 & 1`
  scales by the global light scale b13[0];
- shadow: `_m8 & 1` → atlas slot `_m8 >> 2`, the same 12-tap PCF as spots, × `min(_m5, fade)`;
- BRDF: the same GGX/Burley as the point/spot loop (the class permutations add their terms).
So the light eval allows it. Plan: (1) read the record offsets from the SPIR-V `Offset`
decorations (as for b25) and bind b29 to D-LS (one more push-descriptor binding); (2) walk
the froxel's projector entries (`bg3_cluster_projectors` already counts them) and build a
`Bg3Light`-like record with a direction instead of a position, reusing
`bg3_light_eval_mat` with `Ld = _m9` and the box falloff in place of the range falloff;
(3) trace one ray along `_m9` (cone-sampled by a source angle) with `tmax` = the distance to
the box's face on the light side (the box is the light's extent; casters outside it do not
shadow it); (4) the same delta and `ls_shadowed` logic, the same art-intent rule (the rim
and fill rigs come from `CharacterLight` lists, owner character, flags from their
templates). Validation as 9.3: `ls_debug=4` with the projector pixels included.
(Implemented as `ls_box`: only box lights with a game shadow map are traced; the
`ls_box_unshadowed` A/B key: removed 2026-10-31, see CONSOLIDATION.md C1.)

**User decision 2026-11-03: fixed box shadows approved for Ultra.**
`ls_box=2` with `ls_box_honour_shadow=1` preserves the game's non-shadow-casting art
lights. Ultra uses All; lower presets and repository/release base defaults stay Off.
The visible Off / Shadowed / All control is qualified in Lighting.
Valid same-run ba1 S6 Grymforge 08:02: fixed versus OFF has 8.6% of pixels differing
by > 8 levels, versus 8.5% OFF-to-OFF noise; mean signed difference 0.0, energy within noise.
Unshadowed traces are 0 (old mode: 369M per 120 frames, -5.27% lit luminance).
Local-light pass: 0.45 ms off -> 0.58 ms fixed (+0.13 ms, RTX 5090).
User visual check passed: S6 Adamantine Forge lower-right walkway and central pillar;
the art lights remain visible with Box light shadows = All. The earlier +0.32 ms S6 /
+0.20 ms S7 costs describe legacy mode, not this qualification.

**Art intent correction (2026-11-03, ls-box-art, qualified for Ultra).**
The extractor resolves placement/template `Shadow` (LGHT bit 2) and `CastShadow` (bit 9),
light type 2 = box. The Lua bridge also captures both (`assetdb_api.h` runtime bits 5/6),
but projector selection reads the native b29 record: `_m8 & 1` is its active shadow-map flag.
There is no proven independent projector CastShadow bit or per-projector bridge join.
S6's world `WLD_Main_A`: 892 boxes; (Shadow, CastShadow) 00 = 433, 01 = 89,
10 = 355, 11 = 15. This includes other act 1 regions, not only Grymforge or visible lights.
`WLD_UnderdarkSubs_C` instance: 98 boxes (88 both clear, 10 Shadow only);
`PLT_UND_AdamantineForge`: 5 (4 both clear, 1 Shadow only). See the hashed offline
`tools/live_batches/box_lights_art/asset_flags.json` for scope and source evidence.
Previously mode 1 honoured `_m8 & 1`; mode 2 ignored it for unshadowed boxes.
Now both preserve those lights unshadowed by default. Mode 2 retains its near-plane limit.
Host key `ls_box_honour_shadow=1`; 0 restores old mode 2 for one-run A/B, through push bit 29.
The experimental rf_hit_gi reflection hit path is unchanged (its legacy box policy remains).
Non-box identity: 216/216 byte-identical to main; pp-diff 216 unchanged / 15 box requests changed.
No ABI or variant request changes.
This is a conservative box-only art policy; the point/spot policy above is unchanged.
Qualified S6 batch: `tools/live_batches/box_lights_art`, static OFF / old / fixed, every-window
stats, held-exposure float scene means and diagnostic-free costs. ba1 valid at 08:02;
energy, visible recovery and cost passed as recorded above. Ultra now uses `ls_box=2`.
The following 2026-11-01 mode-2 evidence describes the legacy override.

**`ls_box=2` (2026-11-01, off by default, READY_FOR_LIVE; GUI "Box light shadows: All box lights").**
Why S6 traced only 0.009 box lights per px (lr3 08:49:39 `ls box stats`): not culling or the froxel
list (listed 2.44/px, evaluated 1.141/px), but the rule above: `ls_box=1` traces only `_m8 & 1`
boxes (0.009/px); the other 1.13/px (37.9% of the lit luminance with them) keep visibility 1.
Those 0.009/px were occluded 847,547 of 847,557 times: the ray ran to the box's light-side face,
past the game's shadow view (casters nearer the light than its near plane cannot be in the map).
- **Unshadowed boxes** are traced from the receiver along `_m9` up to the end of the light-side z
  fade-in (uvw.z = `ramp.x` on the z = 0 side, `ramp.y` on the z = 1 side; the editor position is
  the z = 0 face). Geometry in the fade-in is the emitting fixture (the main-menu skull case);
  with no fade-in the segment runs to the face. Receiver inside the fade-in: not traced.
  Visibility only: s = 1, t in {0, 1, unknown = 1}, so the delta c (t - 1) only removes light.
- **Shadow-mapped boxes** are traced only up to their shadow view's near plane (ref = 0 of
  `((z - dp.y) / dp.x) / dw`; "lit iff ref < stored", so depth grows away from the light).
  Unverified: whether the game clamps casters in front of the near plane (pancaking); the
  counter "occluded where the game's atlas term is 1" decides it (mode 1 vs mode 2 at S6).
- Math in `common/box_light.h` (bg3_boxes.hlsli uses it); `box_light_test`: falloff, curves and
  the standard-class terms against a literal transcription of f469db78 (diffuse 9.6e-7 relative,
  specular within its derived bound), exit, near plane and fade-in segments.
- **Accuracy check that samples in normal play** (`ls box bound`, ls_stats, debug 0): the game's
  stored value is transmittance x (direct x mix(1, AO, m0.y) + ambient x AO) + in-scatter, all
  >= 0, so the recompute of its direct local light (every evaluated point/spot/box light, the
  game's own shadow terms, the same AO factors) cannot exceed it. Threshold from the encoding:
  a channel fails when the recompute reaches the upper neighbour of the stored B10G11R11 value
  (`common/ufloat_interval.h`). One-sided (an under-estimate passes); fog transmittance < 1
  could flag. The old `projector validation px` counts only in `ls_debug=4`, hence 0 in lr3.
- **`ls_debug=4` recomputing ~1/4 (S7, sup-boxval 21:04):** debug 4 zeroes AO, and the game
  multiplies direct light by mix(1, AO, m0.y): with m0.y = 1 (S6) game and recompute are both 0
  (P3 273); with m0.y = 0.5 (S7) both keep half. The residual (game 0.0127 vs 0.0027 on px
  without projectors, recompute above game on 0.004% of px) is additive and AO-independent. The
  only such term left in the lighting's output is the game's in-scatter (code reading; S6, where
  debug 4 reads exactly 0, then has none) (`_2986 = fma(_2478, T, inscatter x _m19 x 10 x b13)`, f469db78 line 1623),
  not zeroed by the prep pass, plus its transmittance T. Not fixed in debug 4; the two-sided check is
  `ls_native_direct_validate` (S6 +0.60%, P3 290/291), the in-play one `ls box bound`.
- Device-local counters (set 4 of the box layout, reduced per 8x8 group, copied into rtStats
  385-416 after the timed region): `ls box trace` (per kind: traced, occluded, atlas-lit
  occluded, cut segments, removed) and `ls box bound`.
- Key-off SPIR-V (`tests/ls_exact_k_shader_test.py` against 904c41e): 992 of 1244 requests
  byte-identical; the 252 others are the 60 LS_BOX variants (ls_box >= 1 only) and the 192
  rf_hit_shade variants (`NATIVE_HIT_BOXES`: the same box expressions now from box_light.h;
  GPU fixture 4a-4c pass). Default GI, D-RF and every ordinary D-LS variant are identical.
- Expected cost: +0.15 to +0.3 ms on the D-LS pass (S6 +1.13 rays/px over 2.21; S7 measured
  0.95 -> 1.20 ms with all boxes traced, sup-box-s7). Live job: `tools/live_batches/box_lights`.
- User check: S6 forge and S7 inn with "All box lights": light no longer leaks into the next
  room or through floors; the main-menu skull keeps its beam.

**Forward receivers: design note (later).** Water, glass, particles and VFX are drawn after
the deferred lighting by the game's forward passes, which evaluate the sun with the cascades
and the local lights with the atlas themselves. To make them receive: (a) sun — write the
traced visibility into a screen-space image is not enough (they are not at the G-buffer
depth); trace in a small compute pass over the forward pass's own depth (a prepass of the
transparent surfaces does not exist; the refraction copy in step 13 gives the opaque depth
only), or patch the forward shaders' shadow lookup to read a world-space visibility cache
(SHaRC-like, STATUS backlog 5); (b) local lights — the same: a per-froxel visibility or a
patched lookup. Water is the most visible case and also needs the forward capture of
backlog item 12 to cast. Do it with item 12.

### 9.11 Exact-k + RIS remainder (DESIGN_SOTA_NEXT 1a; 2026-10-27, qualified k1 in Ultra 2026-11-04)

`ls_k` and `ls_rest_samples` (with `ls_main_k`): removed 2026-10-31 (CONSOLIDATION.md C1: over
budget), **re-added 2026-11-01 as a shader variant, off by default** (see "Re-add" at the end of
this section). The history below is the runtime-branch version of 2026-10-27.

Why: Grymforge is over the 0.5 ms budget (0.73 ms with the 9.10 policy; `ls stats`: 6.09
listed, 4.34 evaluated, **3.83 traced** lights per pixel, against 2.1-2.3 in the foundry).

What (`shaders/gi/rt_local_shadows.hlsl` `ls_main_k`, both variants; `ls_k` > 0 with
`ls_samples=0`, `ls_debug=0`):
- **Phase 1**, one pass over the froxel list: the trace conditions and acceptance tests as
  before (`ls_light_traced`), then the light's exact unshadowed `c` (`bg3_light_eval_mat`,
  the surface loaded for phase 1 and reloaded in phase 3 as in 9.9) and `w = lum(c_d + c_s)`;
  a running top-`ls_k` by `w` (registers, insertion) and, for each remainder sample, a
  streaming reservoir over the lights that never enter the top-k or are pushed out of it.
- **Phase 2**: at most `ls_k + ls_rest_samples` rays, same ray setup. The exact set is
  traced in list order, not by rank, so that neighbours with the same set but another
  ranking trace the same light in the same iteration (coherent rays, as the `ls_k=0` loop).
- **Phase 3**: the exact terms of the selected lights only; the remainder sample's delta,
  and its share of the `ao_direct` sums, × `Σ_R w / (n·w_j)`.
- Estimator `Σ_E c_i(t_i − s_i) + (1/n) Σ_samples c_j(t_j − s_j)/p_j`, `p_j = w_j/Σ_R w`:
  **unbiased** (the split is a deterministic function of the pixel; `w_j > 0` wherever
  `c_j ≠ 0`). A remainder sample's scaled term has the luminance of the whole remainder
  (`Σ_R w`) at most: the variance is bounded.
- **Noise for RR** (QUALITY R1/R2): one pcg3d draw per (pixel, frame) from an input domain
  disjoint from the ray directions' (`x | 2^20`) drives both reservoirs (the uniform is
  rescaled after each decision); nothing is reused across pixels or frames.
- Registers (`tools/pipeline_stats.py`): **128 with and without this path** (the policy
  merge had already taken the default variant from 96 to 128); FULL 251 → 252.
- `ls_check=1` still compares the `ls_k=0` path (`ls_main`) with the frozen reference:
  it proves the shared tail (`ls_finish`, moved out of `ls_main`) bit-exact.

**First A/B (jobs `30-lsk-*`, 2026-10-27; optimised pass, no `shader_debug`, one session per
scene; first version, irradiance weight, see below):**

| Grymforge (S5) | ms (median, 300 frames) | rays/px |
|---|---|---|
| `ls_k=0` | 0.730, 0.731 (two sessions) | 3.83 |
| `ls_k=2` | 0.643 (−12%) | 2.76 (exact 1.96 + remainder 0.80) |
| `ls_k=1` | 0.528 (−28%) | ≈1.9 (estimated) |

- Stats (`ls_k=2`): 80.1% of pixels over the cap, 2.33 remainder lights per capped pixel
  carrying 13.9% of the weight; remainder rays occluded 32.7%.
- Campsite (S3): `ls_k=0` 0.965 / 0.965, `ls_k=2` 0.955 (−1%); 1.26 traced lights/px,
  15.1% of pixels over the cap.
- **Not faster per ray.** A linear fit `ms = F + O + rays·c` over the three Grymforge runs
  gives ≈0.135 ms per ray per pixel and a fixed **≈0.06-0.07 ms extra** for the exact-k
  path (k1 and k2 alike, so not the top-k insertion, which grows with k). The candidates:
  phase 1's extra work per light, and the remainder rays (0.8-1 per pixel at k1 and k2,
  each to a different light per lane: incoherent traversal). Registers are not the cause
  (128 either way). The follow-up jobs `31-lsk2-*` separate them (`ls_rest_samples=0`).
- **Images: fireflies.** The `ls_k=2` Grymforge trace has blue fireflies on wall edges
  (dumps saturated at 65,000 after the pass; mean luminance ×18). Cause: the first
  version weighted by irradiance without the BRDF lobe; at grazing view angles the
  specular term over the irradiance grows without bound (`vis ∝ 1/(N·L)` cancels the
  irradiance's N·L), so `c_j / p_j` was unbounded. Unbiased, but infinite in practice.
  Fixed by the exact `w = lum(c)` above (bounded by `Σ_R w`), at the cost of a BRDF
  evaluation per traced light in phase 1. `ls_k=1` and the FULL-variant `ls_k=2` trace
  happened to show none; their means are within the run-to-run spread of two `ls_k=0`
  sessions (−1.3%), and the QA `hard_dark` regions flagged in them are moving characters
  (crops checked), not shadows.

Settings (live): `ls_k` (**default 0**: the follow-up below did not pass; 1-4), `ls_rest_samples`
(default 1; 0 drops the remainder: biased, timing and debug only; 2). Push: `ls_kmode`,
the old spare word `pad` (bits 0-3 / 4-7).

Statistics (`ls_stats=1`, line `ls exact-k stats`, counters 122-127): lights `ls_k=0` would
trace, rays per pixel (exact / remainder), the share of pixels over the cap (k-hit), the
remainder size, its rays' occlusion, and the remainder's share of the weight. In this path
the `ls stats` line's "evaluated" count is 0. `ls_stats=1` costs ≈3-4 ms (FULL variant):
time only from runs without it.

**Follow-up (`31-lsk2-*`, exact weights, 2026-10-27).** Rule set before the run: the default
becomes the largest `ls_k` with Grymforge ≤ 0.69 × `ls_k=0` (0.5 ms of 0.73) and no
`hard_dark` or firefly region against `ls_k=0` (dump maxima after the pass within 2× of
`ls_k=0`'s); otherwise 0 stays.

| Grymforge (S5), one session | ms | vs `ls_k=0` |
|---|---|---|
| `ls_k=0` (again, next session) | 0.780 (0.774) | |
| no rays (`ls_min_radius=1e5`): the floor F | 0.197 | |
| `ls_k=2` | 0.718 | 0.92: **fails** |
| `ls_k=2, ls_rest_samples=0` (biased) | 0.582 | |
| `ls_k=1` | 0.606 | 0.78: **fails** |
| `ls_k=1, ls_rest_samples=0` (biased) | 0.447 | |

Campsite (S3): `ls_k=0` 1.024 / 1.029, `ls_k=1` 0.929 (−9.5%), `ls_k=2` 1.017 (−1%).

- **Where the time goes** (exact rays 1.96 / 0.99 per px at k2 / k1, remainder 0.80, from
  the stats of the first A/B): an exact ray costs **0.139 ms per ray per px** (the two
  `rest=0` runs), against 0.152 in the `ls_k=0` loop (which includes each light's
  evaluation); a remainder ray **≈0.17** (`rest=1 − rest=0` at both k): +22%, from its
  incoherent target (a different light per lane) and its evaluation; and a **fixed
  0.11 ms** over the floor (`0.447 − 0.197 − 0.99·0.139`): phase 1's surface load and the
  exact BRDF of every traced light, which `ls_k=0` evaluates once and this path twice
  (≈0.06 ms with the first version's irradiance weight). Registers are not the cause
  (128 in both paths). That overhead takes most of the saved rays: Grymforge's floor is
  already 0.197 ms, and even 2 rays per pixel (k1) cost 0.41 ms above it.
- **Images pass.** No fireflies: the maxima after the pass are 39.5 (k2) and 47 (k1)
  against 51 / 45 for the two `ls_k=0` runs (the first version: 65,000). Mean of the lit
  HDR (compose) −3.6% (k2), +0.2% (k1) against +1.4% between the two `ls_k=0` runs. Every
  `hard_dark` region flagged on the final frame (S5 k1 3, k2 1; S3 k1 2) is a moving
  character or the animated lava, as two `ls_k=0` frames also differ there (crops
  checked). Pre-RR noise (high-pass of the compose): k1 +13% at Grymforge, k2 +1%;
  unchanged at Campsite; the final frames are within run-to-run spread.
- **Verdict: `ls_k` stays 0.** Neither k meets the budget rule. `ls_k=1` is a valid,
  unbiased cost option (−22% Grymforge, −9.5% Campsite, more pre-RR noise) for a lower
  preset; that is a preset decision, not made here.
- **What would make it pay:** a weight as cheap as the first version's but bounded, so
  that phase 1 needs no full BRDF (e.g. irradiance × a per-pixel bound of the specular
  lobe, the bound evaluated once per pixel); at most ≈−0.05 ms, so k1 ≈0.55 ms: still
  over 0.5. Grymforge's budget miss is the floor and the 3.8 traced lights per pixel;
  1b (ReSTIR) does not remove either. A lower-resolution visibility or a per-froxel
  light cull (lights fully occluded for the whole froxel) are the levers left.

**Re-add (2026-11-01, branch of this worktree; offline, READY_FOR_LIVE).** Why it is back: it is
physically valid (unbiased, same energy, the same exact terms and `ls_finish` tail as `ls_main`)
and nothing superseded it (no ReSTIR, `ls_samples` RIS is the noisier all-light variant). It was
removed only for the 0.5 ms Grymforge budget, which is a preset question, not a correctness one.
- Variant, not a runtime branch: `LS_K=1` in `gi/rt_local_shadows{,_box,_full,_full_box}_k.hlsl`.
  core/gi.cpp builds them only with `ls_k > 0`, `ls_samples=0`, `ls_debug=0`, `ls_check=0`,
  `ls_impl=1` (`common/ls_exact_k.h use_variant`); a twin that fails to build leaves the
  every-light pass (logged `LS_K pipeline UNAVAILABLE`). Key off: 873/873 baseline requests
  byte-identical (`py -3 tests/ls_exact_k_shader_test.py`), 72 LS_K requests compile and validate.
- The selection (top-K, reservoirs, list-order sort, factors) is one shared header,
  `common/ls_exact_k.h`, run by the shader, the CPU test `ls_exact_k_test` (exact set, P(j) =
  w_j / sum_R w, grid mean = full sum, sample weight <= sum_R w) and GPU fixture case 8 (the same
  checks on the GPU, and its selection equals the C++ one: 0 differing cells of 81920).
- Registers (RTX 5090, `tools/pipeline_stats.py`): default 128 = k 128; box 128 vs box_k 168;
  full_k 255 (stats only).
- Counters moved to 350-355 (`kLkStatFirst`; 122-127 are the primary-fade counters now). Logs:
  `ls exact-k: active|inactive (ls_k K, ls_rest_samples N)`, `ls exact-k stats` (ls_stats=1),
  and the `ls: local-light shadow pass GPU` line names `exact-k K + N (variant 0|1)`.
- Release policy (user decision 2026-11-04): exact-k k1 (`ls_k=1`, `ls_rest_samples=1`)
  is automatic in Ultra with Full local shadows and ray-traced geometry. No release control;
  `exp_ls_exact_k` remains a developer override. Lower presets stay Off because the approved
  Full-path evidence does not cover them. Existing LS_K requests are selected; catalogue delta 0.
- Expected cost: the 9.11 numbers (k1 −22% of a 0.78 ms Grymforge pass, −9.5% at camp; k2 −8%
  / −1%). Live gate: job `lsk1-exact-k-inn-s7-fg2` (tools/live_batches/ls_exact_k, S7 inn, one
  launch, NOT queued). Keep k only where its `ls:` GPU median is below the OFF-a/OFF-b spread
  and the scene mean equals OFF within that spread (energy rule: see the live result below).

**Live S7 inn (2026-11-01, `lsk1-exact-k-inn-s7-fg2`, log `lsk1-exact-k-inn-s7-fg2.1.071324.log`;
static camera, OFF / k1 / k2 / OFF, then k1 / OFF / k1 / OFF alternated).**
- Cost (`ls:` GPU median): OFF 0.717 ms, k1 0.555 (−23%), k2 0.654 (−9%); the OFF spread is 0.002.
  k-hit 78.6% of px, 2.84 remainder lights per capped px, 26% of the weight, 34% of its rays occluded.
- First reading (PNG previews of the R11G11B10F dumps p8 / p9): k1 3-4% darker than OFF in every
  alternation (p9: OFF 4.99 / 4.92 / 5.00 / 4.84, k1 4.75 / 4.84 / 4.76, k2 4.74), final image (p14)
  unchanged within 0.1/255.
- **Root cause: not the estimator; the measurement read the game's bloom.** The R11G11B10F dumps
  p1-p9 there are the bloom pyramid (1280x534 down to 160x67 and back up; `FRAME_MAP` step 14), not
  the lighting. Its prefilter (fs `3434e561`, 2560 → 1280) takes 4 bilinear taps, multiplies each by
  `saturate((lum − T)·S)` and keeps the **minimum** of the four (a firefly filter). The minimum is
  concave: `E[min(X1..X4)] < min E[X]` for any noisy unbiased X, so the remainder sample's extra
  per-pixel noise darkens the bloom at the same scene mean. With `rr_post=1` the bloom is built from
  the pre-RR (noisy) scene colour and enters after RR as `+0.2·bloom`. The PNG preview (Reinhard,
  then gamma 1/2.2, point-sampled) is concave too and amplified the gap (float means: −2.6%).
- **Float means of the same dumps (`dumps.json`, luminance, k / mean of the neighbouring OFFs, three
  alternations):** scene colour before post (`5c3fdabc`, 2560 R11G11B10F, RR's input) −0.38 /
  −0.34 / +0.12%, OFF spread 0.89%: **equal energy**. Bloom prefilter output (`3434e561`) −3.6 /
  −3.9 / −0.8%, the same ratio down and up the pyramid (`6bfc74d7`, `779cbf3a`): the min filter.
  k2: scene +0.04%, bloom −1.9% (less remainder noise). Final image: bloom is about 1.8% of it, so
  −3.6% of the bloom is −0.07%, as p14 shows.
- **Checks of the estimator with shadows** (both in the build): `ls_exact_k_test` (CPU) and GPU
  fixture 8e: RGB contributions of other chromas than their luminance weight, visibilities 0 / 1 /
  0.25, the grid mean of the shader's delta (both) and of its `ao_direct` sum `c·t` (CPU) per
  channel equals the full sum within the grid bound (8e worst 3e-4 rel). The CPU test also models the bloom prefilter with the
  estimator's own samples: per-pixel mean exact, min-of-4 mean lower. Read against the shader:
  visibility is a property of the light (its ray stream is keyed by the light, the selection's by
  `x | 2^20`), the selection weight is the unshadowed `lum(c)`, the factor multiplies `c(t − s)`
  and `c·t` alike, the `ls_finish` tail is the same; `ao_direct` gives back nothing here (`m0.y` 0),
  and the `max(·, 0)` clamp could only brighten. No shader change: the default path is unchanged.
- **Energy rule for this option from now on:** the float mean of the scene colour before post
  (`5c3fdabc`, in `dumps.json`), never PNG previews and never the bloom chain. The bloom darkening
  is real but 0.07% of the final image; it applies to every stochastic term the game's bloom sees
  (soft shadows, GI). The fix belongs to the post chain, not to D-LS: build the bloom prefilter
  from RR's denoised colour (rr_post already runs at that point; TRACK_A_RR "RR before post"), or
  leave it (below the final image's precision here). Not done (not an ls file; no visible effect).
- Verdict (user decision 2026-11-04): keep qualified k1 automatic in Ultra, without a release
  user control. Inn cost 0.717 -> 0.555 ms (-23%); scene energy -0.38/-0.34/+0.12% is within
  OFF spread 0.89%. Lower presets remain Off: their qualification is not covered by this evidence.
  Developer mode retains Off/k1/k2 for comparisons. To inspect in game, use S7 inn and the
  existing `ls exact-k: active` / `ls:` GPU lines; this update requires no new live batch.

## Production exact-k driver baseline (rf-ls-regs, 2026-11-05)

`frame_replay --compile-only --compile-pass local-shadows` now builds the production box-k layout
without a pack or dispatch. Native + hair-origin + cascade request, cascade 1000=1, stock hair:
168 registers, driver Shared Memory Size 7168 bytes, Local Memory Size 68719476752 (anomalous;
not a reliable spill measurement). No LS shader change; the existing split/reload remains intact.
No specialization 1032 or setting was added without a demonstrated candidate.

## Offline production LS replay (ls-pack, 2026-11-05)

`replay_pack=1` requests one production Ultra box exact-k frame. The snapshot must
be taken after the light-slot join and immediately around the LS dispatch: game
shadow records/atlas, projector records/2D cookies, pre-LS diffuse/specular, folded
AO, sun mask, auxiliary state, push bytes and actual specialization constants.
Post-dispatch outputs and counters are references. Each offline repeat restores
all mutable inputs before the timed region; paired A/B compares raw output bytes
and counters. No LS shader or variant request is added (request delta 0).

ABI 27 extends the diagnostic dispatch payload for this two-phase snapshot and
additional sets/specs. Rebuild both host and core. Pack generation is off by
default; capture overhead is not a rendering cost measurement. Real inn capture
and replay/live equality remain READY_FOR_LIVE, not qualified. Prepared static
job: `tools/live_batches/ls_pack/jobs/ls-pack-inn.json`, save `The Blushing Mermaid -
96h 39m`, Ultra, 2048 MiB cap, no queued work. See `tools/frame_replay.md` and
`docs/progress/ls-pack.md` for offline verification and limitations.

### Raw compressed replay correction (ls-pack2, 2026-11-05)

The inn capture failed before publication: decoded BC4/BC5 is approximate.
Diagnostic sampled BC1-BC7 allocations now include TRANSFER_SRC, and LS copies
raw blocks over the bound view range. Packs retain VkFormat and swizzles;
replay uploads the blocks unchanged. All decoded BCn paths fail closed for LS.
The 2048 MiB disk cap includes manifest/codec overhead and is enforced before
each block write. ABI stays 27; shader request delta 0. Offline fixtures cover
BC4 atlas and BC5 cookie controls plus byte/sampling identity. Actual inn size
and live-reference equality remain unverified; no live batch ran for this fix.

### Faithful inn replay and exact selector (ls-opt2, offline qualified, 2026-11-05)

Real pack `replay-20261105-224451-1`, frame 1984, Ultra, now matches live:
zero differing bytes in packed diffuse/specular/auxiliary outputs and every raw
counter equal. The replay had appended unused cascade geometry beyond 2 GiB,
making NVIDIA report a zero soup buffer size and causing 92,389 alpha guards.
The harness preserves the captured range when both effective LS ray masks exclude
the cascade. Common/paired specialization overrides retain their geometry needs.

`frame_replay --ab-warmup 128` restores inputs and warms the GPU in the same
submission before each timed dispatch. Default 0 leaves existing runs unchanged.
The final same-module control (40 pairs) is -0.001752 ms, 95% CI
[-0.004256, +0.001008]; every output/counter matches. Earlier cold measurements
were confounded by idle clock transitions. No global GPU setting changed.

Replay-only ablations (eight pairs, nonadditive): visibility traversal costs
0.289352 ms [0.187392, 0.364032], the point/spot loop including selected rays
0.536176 ms [0.438208, 0.658544], and boxes/projectors 0.232368 ms
[0.083952, 0.324592]. Ownership, atlas, BRDF weighting and duplicate early tests
are unresolved. The slot-matching join already ran before capture and is not timed.
Full definitions, intervals and registers: `.scratch/ls-opt2/profile-summary.md`.

Kept developer raw key `ls_fast=0`, SpecId 1040, forced off in release/presets.
For K1/NR1 it skips the independent discarded second reservoir; all other modes
keep the original selector. Forty paired samples save 0.011552 ms, 95% CI
[0.004832, 0.015232], about 1.5% of the warmed 0.77 ms replay. This does not
establish a saving against the inn's live cost of about 2.0 ms. Registers remain
168; shared memory falls from 7,168 to 6,912 bytes. No 128-register claim.
Outputs and counters are 100% bit-equal across 160 measured dispatches with
rt_stats=0 and eight with rt_stats=1; final production SPIR-V equals the timed
candidate bytes. Key-off release proof: 203 identical, 12 equivalent, 0 different,
negative control PASS. Request delta 0, ABI unchanged. No live test ran.
Recheck this inn pack with `--ab-spec 1040=0,1 --repeat 40 --ab-warmup 128
--ab-require-equal`; receipts and build status are in `docs/progress/ls-opt2.md`.
BelowNormal `scripts/build.ps1 -Test` passes all 195 CTests (173.56 s).
