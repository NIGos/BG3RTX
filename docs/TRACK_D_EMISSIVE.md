# Track D: D-EM emissive geometry

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
The D-EM section 13 (13.1 to 13.21) keeps its original numbers. Add new D-EM items here.

## 13. D-EM data: emissive geometry capture (`rt_emissive=1`, 2026-10-27; data side only)

This is the data the emissive effects will consume: emission in GI and reflections, and
emissive light sampling. No effect reads it yet.

| Part | Where |
|---|---|
| Pass and draw identification, records, build, light list, logs | `layer/emissive.cpp` |
| The emissive scene (scene 1) | `layer/bvhsplit.cpp` |
| Effects-pass XFB | `layer/geocapture.cpp` |
| Format, radiance model, lookup | `shaders/rt/em_common.hlsli` |
| Light list | `shaders/rt/em_lights.hlsl` |
| Debug views | `core/emissive.cpp`, `shaders/rt/em_debug.hlsl` |
| Host API | ABI 8 `Bg3HostApi::rt_emissive` |

### 13.1 Which passes and draws emit

AUDIT_ENGINE_DATA 2.2: after the G-buffer, BG3 redraws its emissive meshes, depth-tested,
into the effects target (B10G11R11, render resolution, the G-buffer's D24S8). Compose adds
that target to the lit diffuse + specular. The sky (`c9f013fc`) and the clouds (`68d5b764`)
are drawn into it too.
- **Pass:** 1 colour attachment B10G11R11_UFLOAT + D24S8 at render resolution.
  - The SSS passes (`6f535075`, `d1669194`) and the forward pass have the same signature.
  - So a pass counts as an effects pass only when one of its draws is an EMI variant, and
    the effects target image is taken from those draws.
- **Draw:** its fragment shader is, in the asset DB, a material variant with `pass == 2`
  (EMI).
  - Counted, not captured: decal-class EMI draws (box volumes, `56d5eaaf`) and `*Skydome*`
    backdrops (`f12c2004`). `rt_em_backdrop=1` captures the backdrops, as a measurement.
  - Engine shaders and the shaders the DB does not know are counted too.

### 13.2 Radiance model (one record per draw)

`L(uv) = colour × intensity × glow(uv) [× base colour(uv)] [× global]`, in the effects
target's units. Every factor comes from the material's own data:
- **Colour and intensity:** the DB roles `GlowColour`/`GlowColor` and `GlowMultiplier`,
  read from the game's mapped constant buffer at record time. Unlit helpers use their base
  colour.
- **Glow:** the view bound at the `Glowmap` role's binding, `.r`. Families that switch to
  the texture's colour use `.rgb`.
- **Base colour, "BGM" templates** (Base × GlowMap): the shader multiplies the glow by the
  surface's base colour.
  - `_VT` templates use the virtual-texture albedo. The record carries:
    - the uv transform and the tile-set index (engine CB roles);
    - the tile-set constants (the SSBO entry at set 0 b12);
    - the indirection and cache views.

    `em_vt_albedo()` reproduces the game's Granite lookup, with the same arithmetic as the
    alpha test.
  - The plain templates use the base colour map.
- **Global scale, luminance-emissive materials** (DB flag): `× set 2 b0 [0]`.
  - That buffer is in device memory, not host-visible. The first version read it on the
    CPU; the read failed silently and left the factor at 1.
  - It is now bound as a buffer in the build and read on the GPU: one per build, and
    conflicting bindings are counted.

Families checked against their decompiled shaders (captures `20261026-*`):

| FS | Template | Game's formula | Modelled | First projected check (ours/game) |
|---|---|---|---|---|
| `20864dc6` | CHAR_BASE_GM (+9 aliases) | `c · glow.r · k` | exactly | 1.21 |
| `222986b8` | Base_GM_VT | `mix(c·glow.r, glow.rgb, floor(m1)) · m2` | exactly | — |
| `159a6211` | Base_GM_Pulse_VT | `mix(c·glow.r, glow.rgb, floor(m4)) · m5 · mix(m2, m3, cos(t·m1)/2+1/2) · global[0]` | exactly (pulse at record time; global on the GPU) | 3,318: the global factor was 1 (CPU read of device memory) |
| `7085eee7` | Base_BGM_MaskedShadows_VT | `VTalbedo.rgb · c · glow.r · k` | exactly (VT albedo) | 2.0: the albedo factor was missing (a mean albedo of about 0.5) |
| `aab75fed` | ENV_Tileset_MultipleTextures_BGM_MaskedShadows | `mix(mix(b2(uv·m0), b3(uv·m1), vc.x), b4(uv·m2), vc.y).rgb · glow.r · m3 · (1 − max(vc.x, vc.y))` | b2(uv·m0) · glow.r · m3, i.e. vertex colour vc = 0: the upper bound (flagged approximate) | 5.9: the base colour was missing |
| `48332370` | Helper/FlatColorShadowless | `c` | exactly | (black in the scenes seen) |
| `b86501b6` | Engine_Default_HLOD_GM_VT | three VT layer channels × m0..m2 × global | colour 1 × global (approximate) | not seen on screen |

The vertex colour of `aab75fed` is not captured: it selects which of the three tiles a
vertex uses and where it emits, and XFB records only the position and one UV. Expect
ours/game > 1 on its tiles with vc ≠ 0. If that matters, the fix is to capture a second
output.

### 13.3 Geometry and the build

**Capture.** Every VS is already XFB-patched. Each emissive draw is captured into the
emissive soup, a ring (`rt_em_xfb_mb`, 32 MB). It is converted to camera-relative world
after its pass, in the pass's command buffer, with that pass's camera UBO.

**Build per game submission.** The draws are kept per command buffer. When the game
submits the command buffers that hold a frame's effects passes:
- the layer records a command buffer of its own: one BLAS per draw, the TLAS, the light
  list;
- it submits that command buffer on the same queue, right after the game's batch
  (`em_after_submit`, under the queue lock).

This groups exactly one frame's draws. BG3 records the next frame's effects passes before
this frame's lighting, which broke the two earlier designs:
- **A build at the lighting**, from the draws recorded so far, mixed frames. It could
  include draws whose XFB ran after it, which caused the DEVICE_LOST at the first level load.
- **The draws of the previous frame index only** was safe, but split each frame's draws
  across two builds. That split is the main reason the first projected check covered only
  10.6% of the bright pixels (13.5).

**Latency and camera.** Consumers in later frames bind the latest build: one frame of
latency.
- The header holds the build camera's absolute position.
- `em_shift(camera now)` moves the current frame's lighting-space points into the scene.
  This is exact for static emitters.

**Every build is a full build.** No cached meshes, no refits, no compaction: tens of small
BLAS (36k triangles took 0.31 ms in the first design).
- Nothing depends on an earlier command buffer having run.
- **The emissive scene does not need `rt_bvh_split=1`** (main has the split off for now). It
  uses the split's per-scene machinery (`bvh_available()`), not its mesh cache.

**Our command buffers** are fenced, in a ring of 8. When none is free, the build is skipped
and counted. They inherit the game's image references, so the lifetime tracker keeps those
images alive.

**A separate TLAS, not an instance mask in the main one.**
- The emissive draws redraw meshes that are already in the main TLAS (coplanar duplicates).
  Mixed in, they would z-fight with their own opaque copy.
- Every existing ray (sun, GI, D-LS, D-RF) would also have to change its mask.
- Instead, a ray that needs emission traces the main TLAS, then asks the small emissive TLAS
  for a hit within ±eps of that distance (`em_lookup`).
- Light sampling uses the light list, which needs its own structure anyway.

### 13.4 Data format (`shaders/rt/em_common.hlsli`, set 3 by default)

| Binding | Content |
|---|---|
| 0 | emissive TLAS |
| 1 | InstInfo per instance (16 B): base, stride, **record index**, triangles |
| 2 / 3 | (unused pool) / emissive soup (the scene's camera-relative world, uv at +16) |
| 4 | `EmRec` per draw (256 B): colour × intensity [× pulse], mode (constant, × glow.r, × glow.rgb), flags (luminance, approximate, pulse, base map, VT albedo), glow and base map slots + uv transforms, VT constants and slots, FS hash, class |
| 5 | `EmLight` per triangle (80 B): p0, e1, e2, area, **mean radiance** (all maps at the mip where one texel covers the triangle), **power π·area·lum(L)** (one-sided Lambertian), record, instance, primitive |
| 6 / 7 | textures (glow maps, base maps, VT indirection; 256) / linear wrap sampler |
| 8 | debug-view statistics (host-read) |
| 9 | header: lights, triangles left out, instances, records, pre-exposure, frame, global bound, build camera; power sums per 256 triangles |
| 10 | the game's global scale buffer (set 2 b0) |
| 11 | VT tile caches (16) |

Units and limits:
- Radiance is in the effects target's units: what the game's emissive shader writes into
  the pre-exposed HDR buffer.
- Divide by the header's pre-exposure (lighting s1b13[0]) to get the lighting's units.
- The list is capped at `rt_em_max_lights` (65,536) triangles per build. Triangles past the
  cap are counted ("left out").

### 13.5 Results so far and coverage

Only the first design ran in gameplay: one 300-build window, at the foundry level load,
2026-10-27 05:28. Per build:
- 10.8 emissive draws, 36k triangles, 35.5k lights (429 over the cap);
- total power 1,586, 0 non-finite power sums;
- build 0.31 ms median (p99 0.34); effects passes with the capture 0.011 ms per frame.

The projected check is in the table in 13.2. 29% of the hits fall where the game shows
< 2% of ours, and coverage is **10.6%** of the effects-target pixels brighter than 0.05.

**Where the uncovered bright pixels come from.** Sources: the shader table, the decompiled
shaders and the foundry capture `20261026-183925`. Its units 52-59 are:

| Unit | Draws |
|---|---|
| 52 | sky `c9f013fc` |
| 54, 56 | clouds `68d5b764` |
| 57 | the EMI draws `48332370 20864dc6 aab75fed 159a6211 f12c2004` (21 draws) |
| 58 | `e0e3b441 f081b803 87ea6f1c` (4 draws) |
| 59 | `56d5eaaf` (1 draw) |

- **The build split each frame's draws across two builds** (the second design). Of about
  26 effects-pass draws per frame, about 16 were recorded during the build's own frame index
  and went to the next build. Each TLAS therefore held about half of one frame and half of
  another, which agrees with the 21 EMI draws of unit 57 against 10.8 captured per build.
  The per-submission build fixes this.
- **Unit 58, `e0e3b441` / `f081b803` / `87ea6f1c`.** They are not in the asset DB: no `.bshd`
  of Materials.pak hashes to them.
  - Their decompiled shaders write `vec4(colour, alpha)`, with scrolling noise textures, a
    radial `smoothstep` around a centre and `_14._m1.w` as the alpha.
  - They are alpha-blended VFX planes (magic and glow effects), not surfaces, so they are
    correctly kept out of the TLAS. They belong with the particle case (AUDIT_ENGINE_DATA R10).
  - In this capture the effects target's dumps after units 57, 58 and 59 have the same mean
    (0.000840): units 58 and 59 added nothing measurable.
- **Unit 59, `56d5eaaf`:** an emissive deferred decal (VFX_Decal_..._Emissive), a box
  projected onto the surface. It is not geometry (counted as "decal"), and it added nothing
  measurable in that capture either.
- **`f12c2004` Skydome mountains** (a backdrop, excluded). Its pixels have depth > 0, so
  wherever the mountains are visible (outdoors, through windows) they count as bright and
  uncovered. `rt_em_backdrop=1` measures that share.
- **Sky and clouds** are drawn where the depth is the far plane (depth 0 in the linear
  depth), so they are not counted.

### 13.6 Not done / unverified

- **The per-submission build has not run in the game yet**, and neither has any of 13.2's
  modelling (VT albedo, base maps, the GPU global scale). Every number in 13.5 is from the
  first design.
- Grymforge, Adamantine Forge and Water Queen's House were never run: **lava coverage is
  unverified**. Any EMI material is captured; whether lava is EMI, forward or VFX is open.
- `-Validation`: the one run (first design, with a debug view) added only two IDs,
  `VUID-VkImageMemoryBarrier-oldLayout-01197` and `VUID-vkCmdDispatch-imageLayout-00344`.
  Both came from the effects-target misidentification, fixed since.
- No `-Move` run.
- Candles and fire are forward-pass particles (AUDIT_ENGINE_DATA R10), not captured.

**Settings:** `rt_emissive`, `rt_em_max_lights`, `rt_em_xfb_mb`, `rt_em_log`, `rt_em_tol`,
`rt_em_fx_thr`, `rt_em_backdrop`, `rt_em_lights`.

**Logs:**
- `[em] last N builds`: census, left out, VT albedo missing, global scale conflicts,
  command buffers re-recorded unsubmitted, builds skipped;
- `[em] per build`: instances, triangles, lights, power, GPU cost;
- `[em] emissive families`, `[em] projected check`, `[em] ours/game per family`,
  `[bvh] emissive scene`;
- `rt_em_log=1` adds a line per build and per frame.

**Debug views:** `rt_debug=6` our emission, `7` the game's effects target alone, `8` a ratio
heatmap.

### 13.7 In-game round 2 (the gate, build `74b2338`, `rt_bvh_split=0`) and the fixes after it

The coordinator ran T1-T3 with the per-submission build. No device was lost in any of the
three scenes.

- **S1 `QuickSave_3784`** now opens on a beach and docks view, not the foundry. It had 0
  emissive draws (6 effects-pass draws: engine 1, unknown 3, backdrop 1, other 1), so it is
  not a valid test of the foundry content. Another foundry save is needed.
- **S5 Grymforge: the lava is not in the effects target.** The projected check counts 0
  bright effects-target pixels. A census of the trace's B10G11R11 passes (asset DB names):
  - effects target: `PPEmissiveSkydome`, clouds, `48332370` Helper, and 9 unknown VFX
    draws (`e176c25a` ×6, `e0e3b441` ×2, `12e2531f`);
  - **the forward pass** (55 draws): the lava is `56c5e5a0` **`Environment/ENV_Lava:
    StaticInstancedForward`** (alpha-blend, emissive), next to the VFX particles, the
    waterfall and the water.
  - The decompiled forward shader emits `Glow_Colour × Glow_Intensity × (1 −
    smoothstep(noise) + 0.2·…)`, plus lighting, with fog in alpha.
  - **Fix:** forward-variant materials with the emissive flag that are not VFX, water or
    decals are now captured ("forward" kind). They take the DB roles (colour × intensity:
    the upper bound of the noise factor) and are flagged approximate and forward.
  - Their reference is the forward-lit HDR target, not the effects target, so the projected
    check does not validate them and they are not calibrated.
- **S6 Adamantine Forge: the lava is in the effects target.** It is `1a81f41b`
  `Fluids/FLUID_Lava_Plane_Flowmap` (EMI), and coverage was **98.5%**. Problems found:
  - **Lava ×4.0 (731k px).** The decompiled shader is `blackbody(T) · m23 · (1, 2.5, 0)`, a
    LUT at set 0 b11 at `u = (m0 + (m1 − m0)·h·m22) / 12000`. The heat h comes from two
    flow-noise layers, vertex colour (z, w), distance and a rock mask, and is not
    reproducible from XFB data. Our record had colour 1 × intensity 1.
    - Now modelled as the blackbody LUT at a constant heat `rt_em_lava_heat` (0.5), × m23 ×
      (1, 2.5, 0), which gives the right hue and the game's own temperature range.
    - The scale is left to the **on-screen calibration**: the projected check runs without a
      view every `rt_em_check_frames` (16) frames. For approximate families only, the
      radiance is scaled by game/ours (√ per window, clamped to [1/64, 64]). Exact families
      are never scaled: their ratio stays the validation.
  - **`304be1c7` Base_GM_Pulse_FX ×0.049.** It has the Pulse_VT constant buffer and formula,
    without the global scale, and the pulse fixup applied to `159a6211` only. It now applies
    to every `_GM_Pulse` template.
  - **`159a6211` Pulse_VT captured 0 of 3, and `soup full` 8 per build.** The lava's 152k
    triangles (11 MB of XFB per frame) filled the 32 MB soup's per-frame third first.
    - The soup now **grows to demand** at a build: 4× the frame's demand, up to
      `rt_em_xfb_max_mb` (512), logged "[em] emissive soup grown".
    - Draws captured into the replaced soup are dropped once and counted.
  - **87,051 triangles over the 65,536-light cap.** The list no longer drops triangles.
    Above the cap, k consecutive triangles of an instance share one light, with k chosen so
    that everything fits:
    - the light has their summed area and power;
    - its radiance is the area-weighted mean;
    - its shape is the largest triangle;
    - `prim`, plus `flags >> 16`, gives the cluster's triangles, so a sampler can pick one
      by area.

    Total power is conserved. k is in header word 7 and in the log.
  - Build 0.33 ms median at 152k triangles, 0 non-finite values.

### 13.8 In-game round 3 (`b74c5ab`) and the fixes after it

**Results:**
- **S6 Adamantine** lost the device after 81 s, with only the main menu's Helper draw.
- **S5 Grymforge** lost the device after 169 s. Before that, everything was captured:
  - the forward lava and the Pulse families: 35 draws, 519k triangles, 64.9k lights;
  - coverage 100%, overall ratio 1.13, build 0.52 ms;
  - the calibrated VFX family `10086377` at about 1.0.
- **S8 Water Queen's House** ran clean: 37 draws, 171k triangles, build 0.38 ms. The exact
  families pass: CHAR_BASE_GM `20864dc6` 1.03-1.05, `9228b9cb` and `222986b8` Base_GM_VT
  1.04, VertCut `a7caa456` 1.31. Pulse families read 0. Coverage was 31% (2,548 bright
  pixels, 0.1% of the screen).

**1. Device loss: never build from unchecked vertex data.** The split's fault was traced to
BLAS builds over garbage, unwritten or non-finite vertices, which Vulkan leaves undefined.
- Every captured vertex already goes through `xfb_to_world`, which writes finite positions
  within 1e7 and marks each one w = 1 (valid) or w = 0 (collapsed).
- A new **sanitize pass** (`shaders/rt/em_sanitize.hlsl`) now runs in our command buffer
  over exactly the soup ranges the BLAS builds will read, before them.
- It collapses any triangle with a vertex that is non-finite, beyond 1e6, or not marked (not
  converted this frame) to the origin, and counts it ("invalid soup triangles collapsed").
- If its pipeline is unavailable, the build is skipped.
- A grown soup is zero-filled before its first use, and the effects passes' barrier now also
  waits for that fill.
- Only ranges captured by the draws of the submitted batch are built (as before).

**Images read a frame later.** Consumers read the set in the next frame's lighting, a frame
after the game drew with those glow maps and that effects target.
- `Bg3HostApi::rt_emissive` now takes the consumer's command buffer.
- The layer adds the set's game views, images and buffers to that command buffer's lifetime
  references, so the image-lifetime tracker keeps them alive until it has run. Before this,
  a texture the game freed after its own last use could still be read by our consumer.

**2. The Pulse families read 0.000.** Two causes:
- **`a201d825` Base_GM_Pulse_AlphaBlend** has its material constants at set 1 b1 (intensity
  +24, no texture-colour switch). The fixup read set 1 b0 (the engine constants). It now
  uses the DB's material-CB binding, and the intensity offset says which layout applies
  (+28: with the switch at +24).
- **`159a6211` Pulse_VT** (luminance-emissive) gets the global scale from set 2 b0, a
  per-frame buffer. The build reads it right after the game's batch, but the check (and any
  consumer) reads it a frame later, when that region holds other data.
  - The light-list pass now copies the value into the header (word 11).
  - Consumers use the copy.
- The census now counts material constants that are not host-readable. Possible causes of
  the two small 0.000 families (`38e91cf3` 80 px, `5d837ba1` 23 px) will show there.

**3. Where Water Queen's House's uncovered 69% comes from.**
- **The census of the trace** (`20261027-074655`):
  - all the effects-target content is unit 10's 34 EMI draws;
  - unit 11 (`f081b803`, a VFX plane) adds nothing measurable (same dump statistics).
  - Unit 10 includes the **Skydome mountains backdrop** (`f12c2004`, excluded) and two
    **alpha-blended** glow cards (`a201d825` Pulse_AlphaBlend).
- **Alpha-blended emitters are not in the depth buffer.** The check accepts a hit only at
  the pixel's depth, so such a card in front of a surface counted as a miss, and its own
  hits read 0 (the layout bug above). The check now also looks for an emitter in front of
  the depth.
  - Such pixels count as covered.
  - They are compared per family and their share is logged ("of which ... by an emitter in
    front of the depth").
- **The remaining misses** are:
  - the backdrop (`rt_em_backdrop=1` measures it);
  - characters' glowing parts at their silhouettes: the scene is the previous frame's,
    shifted for the camera but not for the character's own motion.

### 13.9 In-game round 4 (`502aa29`) and the fixes after it

No device was lost in any of the three scenes (`nv153=0`).

| Scene | Run | Coverage | Build median | Passing | Failing |
|---|---|---|---|---|---|
| S8 Water Queen's House | 135 s | 63.5% | 0.40 ms | `20864dc6` 1.02, `9228b9cb` 1.03 | `222986b8` 1.27; `159a6211` 0.000; `38e91cf3` 0.000; `5d837ba1` 0.000 |
| S6 Adamantine | 248 s | 99.8% | 0.43 ms | — | see below |
| S5 Grymforge | 129 s | 100% | 0.53 ms | lava 1.04 | VFX `10086377` unstable at 0.95-1.36 |

In S6 Adamantine:
- the whole lava pool is **`f298bf4d` `ENV_Base_MutipleUV`**, which reads 0.000 over 1.68M px;
- `1a81f41b` Lava_Plane reads 1.55 at scale 0.68, so it was still converging;
- `222986b8` reads 1.92 over 432k px;
- `159a6211` reads 0.000.

**1. Luminance-emissive families read 0** (`159a6211` and `f298bf4d`).
- Both multiply by the game's set 2 b0 [0], and the game rewrites that buffer later in the
  same batch.
- Neither binding it in the build (13.2) nor copying it in the light-list pass (13.8) saw the
  draw's value.
- **Fix:** the value is copied on the GPU right after the effects pass, in the pass's own
  command buffer (`shaders/rt/em_copy.hlsl`), into a slot of our own buffer (a ring of 4096).
  The record carries the slot (`EmRec.glob`).

**2. The lava calibration stayed at 1.0 in Adamantine.**
- The lava that covers the screen there is `f298bf4d`. It read 0 (cause 1), and a ratio
  that low is taken for a forward family and never calibrated.
- `1a81f41b` did calibrate (0.68 in the last window), but at most ×√ per window, and only
  over windows with ≥ 500 px.
- **Fix:** the full correction per window (clamped to ×4), with ≥ 200 px.
- **`f298bf4d` itself:** `glow(uv1).rgb · noise(scrolled uv0)·1.5 · m2 · global`. Its glow map
  reads the **second** UV, which XFB does not capture (the captured UV is the VS's
  lowest-location vec2 output, the first). It is now modelled as a constant (intensity ×
  0.75 × global, the noise's mean), and the map's mean is left to the calibration
  (approximate).

**3. The 0.000 character families** (`38e91cf3` 76 px, `5d837ba1` 25 px).
- The census reports **0 material constants that are not host-readable**, so the constants
  are read.
- `38e91cf3` is exactly `c · glow.r · k` (CHAR_BASE_GM, instanced). It is on moving
  characters, and the scene is one frame old and shifted for the camera only, so its few
  hits land on other texels of a mostly dark glow map.
- `5d837ba1` VertCut_VT also cuts by a vertex-colour mask, which is not captured.
- Both are under 0.01% of the screen: not fixed, reported.

**4. Water Queen's House coverage.**
- No emitter in front of the depth was found (0.0%).
- **Captured:** the census has 4 emissive decals and 1 skydome backdrop per build, neither
  captured. The trace's unit 10 holds every effects-target pixel.
- **Two measurement switches** split the remaining 36.5% between those sources:
  - `rt_em_backdrop=1` captures the backdrop;
  - `rt_em_decal=1` (new) captures the decals' boxes. Their front faces count as covered
    "in front".

  Both are measurements only: a decal's box is not the surface it lights, and the backdrop
  stands in for the sky.
- The rest are the moving characters of point 3.

**`222986b8` at 1.27 and 1.92** is not explained.
- It is an exact family by its decompiled shader (`mix(c·glow.r, glow.rgb, floor(m1))·m2`, no
  discard, UV at location 0).
- The ratio depends on the scene.
- The `rt_debug=6` and `rt_debug=7` dumps of the same view would show whether it is the
  pattern (mip or UV) or the level.

**Merge note:** my conversion-barrier change in `bvhsplit.cpp` (`502aa29`) is reverted
(`dd70778`), so the split agent's barrier audit (`c9297e3`) applies alone when merged.

### 13.10 Round 5 (`db5808a` results), offline fixes

**Decals are captured by default** (`rt_em_decal=1`). They were 55% of Water Queen's House's
bright effects pixels (coverage 63.5% to 99.6%). Their geometry is the projection box, so they
are flagged approximate and calibrated.

**1. `159a6211` Pulse_VT and `f298bf4d` still read 0.000.**
- The projected check's per-family sums were 32-bit, at 1/16 per pixel. Two failures follow:
  - a family's sum wraps past 2^32 / 16 (2.7e8): `f298bf4d` covers 1.7M px, so a mean
    luminance above about 160 wraps;
  - pixels under 1/32 round to 0.
- The sums are now 64-bit (a carry word) at 1/4096 per pixel.
- The copy pass is now checked in the log, not assumed. Every 300 presents,
  `[em] luminance scale copies` prints:
  - the copies dispatched and the copies owed but not recorded (the pipeline is missing);
  - per luminance family, the slot index its records use;
  - the value the GPU copy wrote, read back from our host-visible slot buffer, with min and
    max;
  - the records without a slot (no set 2 b0 bound at the draw).

  If the value is 0 while the game's pixels are bright, the copy reads the wrong buffer or
  offset. If it is non-zero while the ratio is still 0, the fault is downstream (the record's
  `glob` index or the shader).
- The copy's barrier now also makes the result visible to the host.
- Pulse templates: when set 0 b0 (the time) is not host-readable, the pulse factor is its time
  average, (m2 + m3) / 2. Before, the factor was missing altogether. This is counted as `pulse
  time not host-readable`.

**2. `222986b8`: 1.92 in Adamantine Forge, 1.03 in Water Queen's House.**
- The shader is exact.
- The scene-dependent part is the measurement. Adamantine Forge is the bright scene, so the
  32-bit game-side sum is the one most likely to wrap. That would make the ratio too large.
- Fixed by the 64-bit sums. Not yet verified.

**3. `a201d825` 1.41 and `a7caa456` 1.42.**
- `a201d825` (GM_Pulse_AlphaBlend) lost its pulse factor whenever the time was unreadable. A
  stable ratio of about 1.4 is the inverse of a mean pulse of about 0.7. It now uses the
  average.
- `a7caa456` is a VertCut template. Its emission is gated by a vertex-colour mask that XFB does
  not capture, so ours is an upper bound (a ratio above 1, as measured).
- VertCut templates are now flagged approximate, so they are calibrated rather than gated as
  exact.

**4. The lava calibration moved the wrong way.**
- The lava's heat flow swings its own ratio between 1.0 and 2.1 from one window to the next.
- A full step per window chased that animation.
- The scale is now 1 / the exponential average, in log space, of the family's ratio at scale 1:
  weight 1/4, with the first window setting it. That converges to the time average.

**5. Families under 200 px** are marked "under 200 px: not gated" in `ours/game per family`.

**6. The missing `rt_emissive=0` window** ("0.000 ms without (0 frames)").
- The log window counted builds. With `rt_emissive=0` there are none, so it never closed, and
  the next window mixed both phases.
- Windows are now `rt_timing_frames` presents. A window without builds logs `no builds in the
  last N presents`, with the effects passes' median with and without capture.
- The runs also need each live phase to last at least one window: `auto_interval=1200` and
  `-Traces 6`.

### 13.11 em6-WQ (`7f3f89a`) and the fixes after it

- Coverage 99.6%; total ours/game 1.073; build median 0.399 ms.
- **`159a6211` Pulse_VT still reads 0.000.** The copy runs and writes 0.000406901, constant
  over 2,700 reads.
  - The decompiled fragment shader multiplies its output by `_7._m0[0u]` (set 2 b0, a std430
    `float[]`) as is, exactly as our model does. No exponent and no unit factor sits between
    them.
  - So either the word is not the one the draw reads, or the game's bright pixels there come
    from another emitter.
  - The copies line now logs the source's offset (dynamic offset applied), its range, its
    first 4 raw words, and per family the mean luminance per checked pixel: ours, the
    game's, and ours before the scale. One run tells the two cases apart.
- **`7123ab1d` Base_Fresnel_GM_VT (about 10, over 10 px):** the Fresnel view-angle term is not
  modelled. It is flagged approximate.
- **Merge with main (`99d7a02`).** The sun replace took section 12, so D-EM is section 13.
  `rt_debug` 6/7 are the sun's, so the D-EM views are now 16 (ours), 17 (the game's alone)
  and 18 (ratio). Sections 13.1-13.10 keep the old numbers 6-8.
- **The `rt_emissive=0` cost** is now measured in its own session job, not as a live phase.

### 13.12 em7 session runs (`61006ba`) and the fixes after it

Water Queen's House (WQ) and Adamantine Forge (AF) ran without faults. Grymforge (GF) lost
the device during the load (nv153 = 2).

**Verdicts:**
- **Decals by default:** pass.
  - WQ coverage is 97.8-99.0%.
  - The decal family `6a988967` reads 0.88-1.00 once calibrated. Its scale is about 0.02: the
    projection box's radiance is about 50 times the game's mean.
- **`159a6211`: the copied word is the right one.**
  - The source's words 0-3 are s, 1/s, log2(1/s) and -8. For example 0.000406901, 2457.6,
    11.26 and -8; s × 2457.6 = 1.
  - So it is the game's pre-exposure. It varies with the scene and the view (3.0e-4 to
    1.4e-3), and a luminance-emissive material's physical intensity is multiplied by it.
  - Per checked pixel, the game's value is 0 in WQ (under 1/4096) and 0.0066 in AF. Ours is
    0 in both. So the family is dim in the game too, but ours is lower still: in AF, under
    3% of 0.0066.
  - The fault is on our side of the scale, in the level, the map or the pulse. The next run's
    record dump says which. It logs:
    - the record (radiance, mode, texture, flags, slot);
    - the share of hits where ours is exactly 0;
    - ours without the maps (radiance × scale);
    - the constant-buffer words, the time and the pulse.
- **AF lava `1a81f41b`:** 1.047 at the end of the 90 s check (scale 0.73). Pass. The later
  short jobs' windows are partial: 19 and 13 frames.
- **`222986b8`:** AF 1.93 (432k px, the largest family), WQ 1.26. The 64-bit sums did not
  change it, so the quantisation explanation is rejected. It is in the record dump.
- **`304be1c7` Pulse_FX:** AF 0.59-0.67 (894 px). It fails as an exact family and is in the
  record dump.
- **Capture-off cost: not measured.**
  - With capture off, a pass's draws returned before marking the pass as an effects pass, so
    its time was dropped as "not an effects pass". No "without" sample existed, so no
    window logged.
  - Fixed: `em_note_draw` marks the pass.
  - With capture, the effects passes cost 0.066 ms (WQ) and 0.096 ms (AF). The build is
    0.39 / 0.43 ms.
- **GF device loss:** it happened right after `[em] emissive soup grown to 128 MB` at the
  load.
  - The old soup was freed with the frame-based deferral only. The current descriptor set
    (and the consumers that read `emSoup` a frame or more later), plus passes recorded into
    it, could outlive that deferral.
  - Old soups are now retired, and freed only after kRing + kOwn (16) further builds.
  - A pass's conversion now runs on the soup its own draws captured into, not on the
    current one.

**Logs:** lists longer than the log's 2 KB line are split into parts. The cut-off per-family
lines came from this limit.

### 13.13 em8 runs (`b17eaaf`), the merge with main (`eb54bca`) and the fixes after it

There were no faults. `em8-gf-load` passed 2 of 2 runs, each with a soup resize, so retiring
the replaced soups holds.

**Verdicts:**
- **Capture-off cost:** in WQ the effects passes take 0.021 ms per frame without capture and
  0.065 ms with it, so capture adds +0.044 ms (≤ 0.1: pass). The build median is 0.39 ms (WQ),
  0.43 ms (AF) and 0.53 ms (GF, 519k triangles).
- **`159a6211` Pulse_VT: no model error; the family is dark in the game as well.**
  - Two materials share it.
  - In AF and GF the texture-colour switch is 1 and the intensity (+28) is 0. The shader then
    writes `glow.rgb · 0`, and our record is 0, which is correct. The game's 0.0032 per pixel
    there in AF comes from other emitters in the same effects-target pixels.
  - In WQ the switch is 0 and the intensity 1: radiance (0.87, 0.10, 0.003) × pulse. 97% of
    our hits land where `glow.r` is 0, and the game's mean per hit is under 1/4096 too.
  - So the ratio was 0/0.
- **`222986b8`: not a level error.**
  - In AF, 99.6% of the hits are black in both. The game's mean is 1.5e-4 per hit over
    432k px, so the ratio comes from a handful of texels.
  - In WQ it moves with the view: 1.28, 1.17 and 1.02 in the three jobs. That points to the
    mask's filtering (our isotropic mip against the hardware's anisotropic sampling), not
    the level. It is left as a residual.
- **`304be1c7` Pulse_FX:** 0.84 (AF, 894 px) and 1.04 (GF, 55.7k px). Pass.
- **`f298bf4d` ENV_Base_MutipleUV** had radiance 0: the database's intensity slot read 0. Its
  shader multiplies by the material CB's `_m2` (+16), which is now read directly.
- **Uncalibrated decals after a load:**
  - The first `em8-wq-on` window, a new session, had the decal family at 35×. The total
    power was 280 against 7.9.
  - A family never calibrated is now calibrated after 4 checks: about 64 frames, not 300.
- **GF lava `1a81f41b`:** 0.75-0.78 with scale 1.17-1.23 (180k px). It is still converging
  at the end of a run, so the EMA weight is now 1/2 instead of 1/4.
- **The gate is on bright hits** (effects-target pixels above `rt_em_fx_thr`), not all hits.
  Families that are dark in the game (`159a6211`, `222986b8` in AF) no longer pass or fail
  as 0/0.

**The emissive scene's own acceleration structures.**
- Main's split rework (versioned scene slots, TLAS slots and build records) conflicted with
  the 3-scene extension of `bvhsplit.cpp` everywhere. Its fault analysis also ties device
  faults to destroying AS objects at run time.
- The emissive scene therefore no longer uses the split. `bvhsplit.cpp` is main's version
  unchanged. `layer/emissive.cpp` `em_as_build` builds the scene itself:
  - one BLAS per build, with one geometry per draw (the sanitized soup, no indices);
  - a TLAS with that BLAS as its one identity instance;
  - per ring slot, BLAS and TLAS objects that are reused while they fit, so AS objects are
    created or destroyed only on growth;
  - one shared scratch buffer.
- `em_lookup` reads the geometry index as the draw's `emInfo` entry. `em_lights` reads
  identity per-geometry instances.
- The host ABI goes to **9**: `rt_scene_set` took 8 on main.

### 13.14 em9 runs (`6786dd6`, own acceleration structures) and the merge with main (`253e437`)

Five runs, with no faults.

**Verdicts:**
- **Validation:** pass.
  - `em9-af-validation` gives `VkShaderModuleCreateInfo-pCode-08737` ×2,
    `vkCmdDraw-None-09600` ×6 and `vkCmdWaitEvents2-pEvents-10788` ×6. All three are in main's
    baseline (`captures/validation.log`).
  - None names our acceleration-structure builds or our queue submission.
- **WQ:** pass.
  - Coverage 98.5% / 99.8%.
  - Total ours/game, window by window from a fresh load: 0.80, 0.93, 0.95, 0.96, 0.95, 1.03,
    1.05. The first-window 35× is gone (calibration after 4 checks).
  - `222986b8` 1.02-1.05 (≥ 200 bright px); decals 1.03-1.09.
- **GF:**
  - Lava `1a81f41b`: 1.23, 0.91, 0.89, 1.08, 0.91, 0.99, last 1.035. Pass.
  - `304be1c7` 1.04 (12.8k bright px). Coverage 100%.
  - **Build median 0.616 ms**, against my 0.6 ms criterion. It was 0.53 ms with the split's
    per-draw BLAS. The cause is 519k triangles in one BLAS with `PREFER_FAST_BUILD`. Marginal.
- **AF:**
  - Lava 1.02, 0.92, 1.14, 1.21. Pass. Coverage 100%.
  - `f298bf4d` is dark in the game (8 bright px of 1.68M) and ours is 0, since its intensity
    at CB +16 is 0. Consistent.
  - **`222986b8` 1.93 (381 bright px) and `304be1c7` 0.64 (237 bright px) fail as exact
    families.**
  - The same shaders pass elsewhere: `304be1c7` 1.04 over 12.8k bright px in GF, and
    `222986b8` 1.02 in WQ. In AF they are a few hundred bright pixels inside large dark
    families: 99.6% of `222986b8`'s 432k hits are black. Their game pixels also take the
    additive glow of other emitters (lava, particles).
  - The all-hit ratio mixes those effects. A ratio over bright hits only is now logged
    beside it (`bright`) and is the gate. The calibration keeps the all-hit energy ratio.
    This needs an AF run.
  - In the validation run (a fresh load, slowed by the layers) the lava's last window read
    0.74 while its average was still converging. Informational.

**Merge with main (`253e437`, SHaRC):**
- `common/host_api.h` defines `BG3RTX_HOST_HAS_RT_EMISSIVE`, so core/gi.cpp's SHaRC
  emission injection (`BG3RTX_SHARC_EM=AUTO`) compiles in. The core DLL carries
  "D-EM effects target", not "built without D-EM".
- The injection reads `Bg3EmissiveInfo::fx_view/fx_image/fx_layout/fx_width/fx_height`, which
  `em_scene` fills from the effects target seen this frame (fresh within 2 frames).
- The `sharc_*` shaders compile with the layer's flags: `-enable-16bit-types` and the SDK
  path.

### 13.15 Derived gates (QUALITY.md R0 addendum)

The 0.8-1.25 ours/game band, the 200 px cut and the 0.6 ms build limit were not derived.

**ours/game per family** (`scripts/qa/em_gate.py`): the tolerance comes from the ratio's own
noise.
- **Temporal noise:** the window-to-window std of ln r at the run's fixed view, measured.
- **Counting noise:** 1/N over the N bright px per frame. N is an upper bound on the
  independent ones, which errs toward failing.
- **Exact families:** σ² = s_w²/W + 1/N on the mean over windows.
- **Calibrated families:** σ² = s_w² + 1/N on the last window.
- **Pass:** |ln r| ≤ z·σ, with z two-sided at ALPHA_FW / families (Bonferroni, as
  `scripts/qa/thresholds.py`).

**Applied to em9:**
- **WQ:** pass.
- **AF:**
  - lava: pass;
  - `222986b8` 1.93 and `304be1c7` 0.635: fail;
  - the particles at 1.73 with N 42: fail.
- **GF:**
  - lava and particles: pass;
  - `304be1c7` 1.038: **fail**. Over 12.8k bright px the counting noise is 0.9%, so a +3.8%
    bias is real. Candidate causes: the mask filtering (our isotropic mip against the
    hardware's anisotropic sampling) and the B10G11R11 effects target. The target is
    unbiased per pixel, so the filtering is the more likely one.
- em10 logs the bright-hit ratios.

**Coverage (≥ 95%)** is review-only until a derivation exists.

**Build cost:** there is no D-EM entry in SETTINGS_DESIGN's split.
- Proposed: ≈ 0.7 ms (capture 0.044 + build and light list 0.44-0.62 ms, measured), out of
  Ultra's static headroom (≈ 5 ms) and pan headroom (≈ 2.5 ms).
- Until that entry exists, the cost is review-only.
- The planned optimisation is a BLAS refit while the draw list and the triangle counts are
  unchanged. Our command buffers are fenced and never dropped, unlike the game's.

### 13.16 Budget entry and the filtering hypothesis

**Budget (approved).** SETTINGS_DESIGN §5.4, row #8: "D-EM data" at 0.7 ms, taken from the
pan headroom.
- It is measured: 0.044 ms of capture plus the largest build, 0.62 ms in GF.
- The pan ledger, against the 15.4 ms line:

  | Item | Claim | Headroom left |
  |---|---|---|
  | Ultra pan | 13.4 ms | 2.0 ms |
  | SHaRC | ≤ 0.6 ms | 1.4 ms |
  | D-EM | 0.7 ms | **0.7 ms** |

- The static frame is ≈ 12.2 ms.
- The BLAS refit planned after em10 lowers the D-EM claim.
- The 0.7 ms is an optimisation target, not a gate (user directive: budgets never degrade a
  physically better result on Ultra). The derived ours/game gate decides the merge.

**Is the mask's filtering the bias** (AF `222986b8` 1.93 and `304be1c7` 0.64, GF `304be1c7`
+3.8%)?
- The game samples its glow maps with implicit derivatives: hardware LOD and anisotropic
  filtering. Our check samples at an isotropic ray-cone LOD.
- Replicating that offline needs the glow textures and UVs dumped and a BC decoder. Neither
  exists here, and the game's paks are not extracted.
- The replica therefore runs on the GPU instead, over the game's own bound texture,
  live-switchable in the check (`rt_em_filter`):
  - **The derivatives:** the UV's change per screen pixel, as the game's fragment shader
    has it. Each comes from the rays through the next pixel in x and in y, met with the hit
    triangle's plane (ray differentials, `em_uv_on_plane`).
  - **1:** the hardware's isotropic LOD, log2(max |dx|, |dy| in texels).
  - **2:** anisotropic filtering as the Vulkan spec's reference describes it: up to
    `rt_em_aniso` (16) trilinear probes along the footprint's major axis, at the LOD of
    major / probes (`em_sample_grad`).
  - **0 (default):** unchanged.
- The game's sampler state (its anisotropy level, any LOD bias) is not tracked. 16× is
  assumed: BG3's default, and graphicSettings.lsx has no override.
- The em10 jobs `af-filt1/2` and `gf-filt0/1/2` measure the three modes per family with
  `scripts/qa/em_gate.py`. Whichever mode removes the biases becomes the default for every
  `em_lookup` consumer. If none does, the hypothesis is rejected and the next candidate is
  the B10G11R11 target and the alpha blending of the overlapping emitters.

### 13.17 em10: the filtering test, overlap and the refit

Scored with `scripts/qa/em_gate.py`.
- **Exact families** are gated on the `bright` ratio: their per-pixel radiance is modelled.
- **Calibrated families** are gated on the all-hit (energy) ratio: only their mean is
  modelled, and it is what the light list carries.

**Filtering hypothesis: rejected.**

| Family | Scene | filter 0 | filter 1 | filter 2 |
|---|---|---|---|---|
| `304be1c7` (bright ratio) | GF | 0.998 | 0.861 (fails) | 0.992 |
| `304be1c7` (bright ratio) | AF | 0.417 | | 0.424 |
| `222986b8` (bright ratio) | AF | 1.005 | | 1.006 |

- The GF +3.8% and the AF 1.93 of `222986b8` were the all-hit metric, which includes the dark
  texels. On bright pixels both match.
- **The filter stays 0** (the ray-cone LOD). `rt_em_filter` and `rt_em_aniso` (filters 1/2):
  removed 2026-10-31, see CONSOLIDATION.md C1.

**AF `304be1c7` at 0.42 is independent of filtering.**
- The candidate: other emitters on the same ray, such as alpha-blended glows or particles
  over the pulse lights near the lava. The game's pixel holds both emitters; ours holds only
  the surface at the depth.
- The check now looks for a captured emitter in front of each bright hit (the front half of
  the ray). It leaves those pixels out of the bright sums and counts them (`overlapped`).
- Emitters that are not captured cannot be detected this way.

**Statistics.**
- With W windows the temporal spread rests on W - 1 differences. σ's degrees of freedom are
  now Welch-Satterthwaite, and the band uses the Student t quantile.
- Runs with fewer than 2 windows after the first are not evaluated. The AF jobs had only 2
  windows in 90 s.

**BLAS refit (the optimisation toward the 0.7 ms target).**
- A ring slot whose last build had the same geometries (keys: material, vertex and index
  buffers and offsets, draw arguments, triangles) is refit in place (`MODE_UPDATE`,
  `ALLOW_UPDATE`).
- Every `rt_em_rebuild` (30) uses of a slot it rebuilds instead.
- Our command buffers are fenced and always submitted, so the refit source is always built.
- Logged as `[em] BLAS: N refits, M full builds`.

**SHaRC with D-EM:** `sharc: D-EM effects target injected`, no fault (em10-af-sharc).

### 13.18 em11 (`0e57c99`)

There were no faults.

**Validation:** pass. `pCode-08737` ×2, `vkCmdDraw-None-09600` ×6 and
`vkCmdWaitEvents2-10788` ×6, all in main's baseline. None of them is on the acceleration
structure update.

**Derived gate (`em_gate`):**

| Scene | Windows | Result |
|---|---|---|
| WQ | 15 | all 5 families pass |
| GF | 11 | all 3 families pass: lava 1.055, `304be1c7` 0.999, particles 0.79 |
| AF | 8 | 5 of 6 families pass: lava 1.006, `222986b8` 1.005, `4d3f7dab` 1.006, `8bcd0aff` 1.014, particles 1.13 |

**AF `304be1c7` fails at 0.360** (bright ratio, 236 bright px; all-hit 0.632).
- **0 overlapped:** no captured emitter lies in front of its bright pixels, so the overlap
  explanation is rejected for captured emitters.
- **s_w 0.006:** steady across windows, while the pulse varies by ±18% over 6.3 s. The pulse
  phase therefore matches.
- **The pattern:** ours is relatively high where the game is dark (all-hit 0.63) and low
  where it is bright (0.36). That points to the glow mask being sampled in the wrong place,
  not to a wrong level.
- **The candidate:**
  - The fragment shader reads its UV at location 0 (vec2).
  - The XFB patch captures the vertex shader's lowest-location **vec2** output
    (`layer/spirv_xfb.h`).
  - If AF's vertex shader for these draws writes location 0 as a vec4 (packed UVs), a
    different vec2 is captured.
  - GF's draws of the same fragment shader pass at 0.999, so their vertex shader differs.
  - The next step is to log the captured UV location per family, against the fragment
    shader's UV input location.
- **Other unconfirmed candidates:**
  - the 16 unclassified ("unknown") effects-pass draws per build in AF, which can add to the
    same pixels;
  - coplanar overlays, which the front-half test cannot see.

**Refit** (`[em] BLAS` per 300 builds):
- WQ: 292 refits / 8 builds. Build median **0.095 ms**, from 0.39.
- GF: 96 / 204, 0.61 ms.
- AF: 35 / 265, 0.50 ms.
- The geometry key included the vertex and index buffer offsets. The game streams dynamic
  and instanced geometry through per-frame ring offsets, so the key changed every frame.
  - The key is now the material, index buffer, triangles, draw count, instances and kind.
  - A refit of another mesh with the same counts stays correct; only its BVH is worse, and
    the rebuild every 30 uses bounds that.

### 13.19 The UV-slot check

**Question.** Is the captured UV the one the fragment shader reads? The XFB patch
(`layer/spirv_xfb.h` `patch_vs`) captures the vertex shader's lowest-location float2
output. The fragment shader reads its texture coordinate at its own Location, which
can be a wider vector.

**In-game check (every pipeline).**
- `stage_io` parses every module's user interface: Location, Component and float component
  count.
- `uv_match` gives each VS/FS pair a verdict:
  - **ok:** the FS's lowest float2 input is at the captured location;
  - **VS wider:** the FS's UV location is a vec3/vec4 output of the VS;
  - **FS elsewhere:** the FS reads its float2 at another location;
  - **no FS float2**;
  - **unknown:** pipeline libraries.
- Logged as:
  - `[geo] UV slots ...`: totals over all patched pipelines, for every user of captured UVs,
    including EN2's hit materials;
  - `[geo] UV slot mismatch`: each mismatching VS/FS pair, the first 64;
  - a verdict per family in `[em] emissive families`.

**Offline, over every traced pair** (`scripts/qa/uv_slots.py`: 510 distinct VS/FS pairs from
all traces, all modules dumped):

| Verdict | Pairs |
|---|---|
| ok | 433 |
| FS without a float2 input | 77 |
| mismatch | 0 |

- In those 77 pairs the FS reads location 0 as vec4 or vec3, the VS writes the same width,
  and the VS has no float2 output. So nothing is captured as UV: a gap, not a wrong UV.
- **The capture's UV choice is never wrong in the traced scenes.**
- The `304be1c7` draws use VS `f4b35620d54abc6c` in both AF and GF. That VS stores its float2
  input straight into its only float2 output, location 0, and the FS reads `in vec2` at
  location 0. **The UV hypothesis is rejected for `304be1c7`.**

**What else is known about AF `304be1c7`.**
- It shares its glow texture (`s1b1`) with `159a6211`, uses the same VS, and draws 3
  instanced meshes.
- Coverage is 100%, so the geometry matches the depth.
- No captured emitter overlaps it.
- Its pulse phase matches.

**Next: per-pixel comparison.**
- `rt_debug` 19 and 20 show one family alone (`rt_em_dbg_fam_hi/_lo`): ours (19) or the
  game's effects target (20) at its hits, black elsewhere.
- The em12 jobs `af-view19/20` trace both for `304be1c7`. The images show whether the
  mismatch is a pattern (mask, UV transform, texture) or a level (constants).


### 13.20 Open items and remaining tests

Kept from the former D-EM branch handoff (`archive/P2_EMISSIVE_LEGACY_HANDOFF.md`),
only what 13.1-13.19 do not already say.

**Still open after em11/em12:**
- AF `304be1c7` (13.18, 13.19): read the em12 `af-view19/20` per-pixel images.
- GF build median 0.616 ms against the 0.7 ms optimisation target (not a gate).
- Coverage (>= 95%) is review-only until it is derived.

**R4 (when a foundry save exists): S1.** Same command and criteria as the derived gate.
Foundry families seen in round 1: `20864dc6` exact, `7085eee7` exact (VT albedo),
`aab75fed` approximate (vertex colour), `159a6211` exact.

**R5: cost, validation and motion (S6).**
- Cost: from the `rt_debug=0` and `rt_emissive=0` windows. Build <= 0.5 ms. Effects passes
  with capture minus without <= 0.1 ms. The calibration check (every 16 frames) must not
  show in frame pacing.
- Validation: `auto-run.ps1 -Background -Validation -Save "Adamantine Forge" -Traces 2
  -Set rt_debug=18`. Pass: no IDs beyond main's baseline, none naming our queue
  submission, acceleration-structure builds or updates, and no layout IDs on the
  effects target.
- Motion: `auto-run.ps1 -Background -Move -Save "Adamantine Forge" -Traces 3`. Pass: no
  device loss, 0 non-finite values in the dumps.

**Known gaps.**
- Lava texture detail: the calibration matches the mean per family only. The heat field
  (bright cracks, dark crust) is not in the data; the light list's power is right on average.
- Grymforge's forward lava is not validated: the reference would be the forward-lit target
  before and after the lava's draw.
- `aab75fed` vertex colour (approximate, calibrated) and the HLOD family `b86501b6`.
- Not captured: candles and fire (forward particles) and the unit-58 VFX planes.
- One frame of latency: consumers apply `em_shift()`.

### 13.21 Policy out of the host (2026-10-31, partial step done)

**Done 2026-10-31 (ABI 24):** the record policy and the calibration rule run in the core (`core/em_policy.cpp`); the layer keeps the views, the luminance slots, the calibration state and the build. The core reads the bindings itself during the draw hook instead of a raw copy. Proof: `emissive_core_test`, bit-identical against the old layer path. Without a core a draw has no record. Still to do: a live check with `rt_emissive=1` (record count, projected check and calibration log the same as before).


Done: the record formulas (make_record, family_fixup), the EmRec layout and the rt_em_calib
rule are pure functions in `common/emissive_policy.h`, pinned by `emissive_policy_test`. The
layer still calls them through a reader of the draw's bound CBs and views: a change still
needs a game restart. No ABI change (23). Remaining plan, one ABI bump (24):
1. Layer captures raw inputs per draw instead of a record: the material CB (set 1 b0/b1,
   96 B), the time word, the VT engine CB words and SSBO entry (128 B), which views were
   bound, the luminance slot. A fixed `Bg3EmRawDraw` (~512 B) in PDraw.
2. `Bg3CoreApi::em_records(raw, n, out)` and `em_calibrate(family ratios -> scales)`, called
   by the layer's build at submit (core lock held); without them (old core) the layer runs
   `em::make_record` itself, so behaviour is unchanged.
3. Gate: with rt_emissive=1 the core's records equal the layer's byte for byte (an in-frame
   compare under rt_stats, ls_check style), then remove the layer path.
Risks: core hot-reload during a build (take the core lock), CB bytes must still be read at
record time (the game rewrites them), views stay layer-owned.

### 13.22 AF `304be1c7`: formula re-derived, family excluded (`rt_em_fx_pulse`, 2026-11-01)

**The formula is exact.** The dumped fragment shader (`captures/alpha_cache/304be1c71df194cb.glsl`)
emits `mix(m0 * glow.r, glow.rgb, clamp(floor(m4))) * m5 * mix(m2, m3, cos(time * m1) * 0.5 + 0.5)`:
GlowColour s1b0+0, GlowMultiplier s1b0+28, the switch +24, the pulse +12/+16/+20, time set 0
b0 [0], Glowmap s1b1 through the global sampler s0b7 (the one every passing family uses). No
global scale, no second layer, no exposure term, alpha 1, blend off, depth EQUAL (run 295).
`make_record` + `family_fixup` reproduce it term for term (`emissive_policy_test` 9b pins the
record, switch off and on). The asset DB agrees (`py -3 -m tools.assets shader 304be1c71df194cb`).
So the 0.36 is not a missing multiplier in the record.

**Already rejected:** UV slot (13.19), filtering (13.17), pulse phase (s_w 0.006), captured
overlap in front (13.18) and at equal depth (run 295: multi-draw receivers 0).

**Still open, needs a live observable:** (a) an uncaptured draw (AF has 16 unclassified
effects-pass draws per build) writing the same pixels later; (b) a mask-lookup pattern error
(the em12 `af-view19/20` images, if not yet read); (c) back faces the native draw culls.
Side finding: AF `159a6211` (same glow map, `_VT`, luminance) reads ours 0.000 (138 bright px,
not gated): its luminance-scale copy reads 0 in AF. A lead for (a) if both draw the same mesh.

**Exclusion.** `rt_em_fx_pulse=0` (default): `make_record` returns an empty record flagged
`kFlagExcluded` (nothing read or bound); the layer does not capture the draw and counts it:
`draws of excluded families (rt_em_fx_pulse=0: 304be1c7) N per build` on the `[em] soup` line;
`[em] emissive families` shows its draws with 0 captured. It is hot (core cfg read per draw).
This also drops it in GF, where it passed: conservative until the AF cause is known.
**Live job:** `tools/live_batches/em_forge/` (0/1/0 in one AF run, record dump, rt_debug
19/20 pair). Lift the exclusion only when em_gate passes AF and GF with `rt_em_fx_pulse=1`.

**em-forge result (ef1-af-em-fg2, 2026-11-01), offline reading (em-fix2).**
- `159a6211` reads 0 because its constants make it 0: words 6-7 are switch 1 and
  GlowMultiplier (+28) 0, and its shader (`159a6211a89e8abd.glsl`) multiplies by `_m5` = 0.
  Time and pulse are right (79.02 -> 1.508, 81.1 -> 9.255 = 1 + 9 (cos t / 2 + 1/2)). No fix;
  `emissive_policy_test` 9c pins the logged words. The game's 0.0016 there is other emitters.
- `304be1c7` (fx windows): bright 0.841 (0.818-0.858), all-hit 0.91, 0 overlapped, s_w 0.017.
  AF CB words (p2 dump): colour (0.8513, 0.1011, 0.0026), m1 1, m2 7, m3 10, switch 0,
  intensity 1. Our record equals the dumped one (9c), so the per-record terms are exact.
- **The deficit is an additive, non-pulsing term in the game's pixel, not a factor.** Per
  window the ratio follows the bright-pixel count (the game's pulse phase) with correlation
  0.995 (bright) and 0.994 (all-hit), and it is below 1 in all 20 windows. With game = ours + E,
  E / ours goes 0.167-0.222 (x1.33) while the window-mean pulse swings about x1.3. A wrong
  factor would give a constant ratio; a time, phase or frequency error would average to 1
  over a period and swing above 1. Mask channel (switch 0: `glow.r`, mode 1) matches. So
  E is about 19% of ours at the mean pulse, on the bright texels, and depends on the view
  (0.36 on 10-28, 0.84 now).
- Draws after it in the effects pass (trace `20261101-004514`): `f298bf4d` x5, lava x6,
  `ded335a6`, `159a6211` x6 (same glow map and VS), `4d3f7dab` x6, `10086377` particles x3.
- **Evidence still needed:** the effects target at the family's pixels right after its own
  draw against the pass end (a per-draw dump or a mid-pass copy), or the blend state of
  each later draw that covers them. Note: the record dump only fires outside 0.8-1.25, so
  0.84 logged no dump; the em-forge plan assumed one. The exclusion stays (`rt_em_fx_pulse=0`).

### 13.23 First consumer: emission at ray hits (`rt_hit_emission`, M4.4, 2026-11-01; offline, READY_FOR_LIVE)

Key `rt_hit_emission=0` (live; GUI Experimental "Emission at ray hits", `exp_hit_emission`: Off / Hidden
hits / All hits; needs `rt_emissive=1` at startup and `rt_hit_material=1`; acts through `gi_hit_direct`
and/or `rf_hit_shade`). Plan: PLAN_M4_REFLECTIONS_GI.md M4.4.

- **What.** A GI bounce ray or a reflection ray whose main-TLAS hit lies on a captured emitter adds that
  emitter's record radiance (`shaders/rt/native_hit_emission.hlsli`): `em_lookup` within
  `rt_em_tol` x the hit's camera distance + 1 cm, `em_radiance` at the hit's uv with the ray-cone
  footprint. This is the projected check's lookup (`em_debug.hlsl`), so a hit gets exactly the record the
  check validated. Units need no conversion: the lit buffer and the effects target are both pre-exposed
  (compose adds one to the other as is).
- **Record policy.** Exact families: used. Approximate families: only once calibrated
  (`kEmFlagCalibrated`). Excluded families (`kFlagExcluded`; `rt_em_fx_pulse=0` drops `304be1c7` at
  capture, so it is never in the TLAS): never. Two-sided, as the check.
- **Where (energy).** The screen value has no emission: `prevRadiance` is the lit diffuse + specular
  that `gi_composite.hlsl` keeps before compose adds the effects target (`sharc_inject.hlsl` adds that
  target explicitly for the same reason). So nothing on screen counts emission today, and adding it
  where a value lacks it never counts it twice:
  - `1` hidden hits: GI hidden hits where fallback0 added no bounce (the `gi_hit_direct` domain; the
    emission adds even when the material is unknown, the occlusion is unchanged), and shaded D-RF tier (b)
    hits (L_b + emission). Unshaded tier (b) hits keep the probe.
  - `2` also screen hits: GI on-screen hits and D-RF tier (a) hits. The rf_hit_shade tier (a) check takes
    L_a before the addition (both sides of that check lack emission).
  - SHaRC hits are never touched: the injection already adds the effects target (`sharc_em`).
- **Layout.** No ABI change, no push descriptor: the host's emissive set (`Bg3HostApi::rt_emissive`,
  contract `common/hit_emission_bindings.h`) as GI set 2 (the empty one) and D-RF set 5 (after SHaRC's 4).
  Variants: `gi/gi_trace_rt{,_sharc}_hit_{direct,sun}_em.hlsl`, `gi/rt_reflections{,_sharc}_hit_shade_em.hlsl`.
  The core (`hit_em_set`) binds the latest build when it is at most 3 frames old; else the variant without
  emission runs. Push fields reused (unused elsewhere): `ls_kmode` bit 0 screen hits, `rt_ao_radius` rt_em_tol.
- **Log** (every 300 frames; GI under `rt_stats=1`, D-RF under `rf_stats=1`):
  `hit emission (300 frames): GI hidden hits: lookups N, emissive N, skipped N, added weighted luminance X;
  GI screen hits: lookups N, emissive N, skipped N, added X; rf tier b: lookups N, emissive N, skipped N,
  added luminance X; rf tier a: lookups N, emissive N, skipped N, added X; skipped by family: excluded N,
  approximate uncalibrated N; clamped N; capture exclusion rt_em_fx_pulse=0 (304be1c7 not captured; its
  draws are on the [em] soup line)`. Counters 331-349 (luminance x 1024, clamped at 64).
- **Offline gates.** `py -3 tests/hit_emission_shader_test.py`: every key-off request of the merge-base
  compiles byte-identical; every `*_em` request compiles, passes spirv-val and its contract (`--em`).
  `native_hit_gpu_fixture` case 7: coplanar emissive quads in an emissive TLAS of their own, the real
  `native_hit_emission`: constant record exact, x mask.r exact, policy (approximate 0, calibrated used,
  excluded 0), no emitter and an occluder in front give 0, the `em_shift` latency shift sign.
- **Live gate (not run).** S5/S6 lava and S3 braziers, one run, `rt_hit_emission` 0/1/2/0 with
  `rt_stats=1 rf_stats=1`: the log line above nonzero; per pixel GI never darker than key off (it only
  adds); the `rt_debug` 16-18 D-EM checks unchanged; crops show the glow in bounce and reflections.
  Cost: `gi: trace pass` and `rf: reflection pass` GPU medians ON minus OFF. Expected small (one ray
  query into a TLAS of tens of instances per eligible hit), not measured.
- **Not done: emitters as light sources at hidden hits** (one sampled emitter + shadow ray from the light
  list). Not cheap and clean yet: (a) the light list has per-group power sums but no prefix CDF, so a
  sampler would walk up to 256 groups per hit, or the build needs a scan pass; (b) many emitters
  (lava, braziers) already have artist point lights in the game's list: lighting with both counts that
  light twice, so it needs a per-family "has an authored light" policy first; (c) the screen's own
  direct lighting lacks emitter light too, so the hit-only version would make hidden surfaces brighter
  than the same surfaces on screen. Next step: a scan pass for the CDF in the build, then the policy,
  then screen + hits together.

### 13.24 The projected check's cost: device-local counters, sliced (2026-11-01; offline, READY_FOR_LIVE)
- **Finding (inferred, not measured).** S6 Adamantine Forge, m4p3-s6-fg2: `rt_emissive` 0 -> 1 raised the 300-frame
  average `gi: GPU cost` 1.65 -> 10.4 ms while every pass median stayed flat: one frame in 16 cost about 140 ms more.
  That frame ran the calibration's projected check (`em_debug.hlsl` mode 0, from run_post inside the gi timer): up to
  3 emissive ray queries per pixel (at the depth, in front, the overlap test) and, per pixel, 4-14 atomics into
  `emStats`, which lives in the host's host-visible region (PCIe; the META_AUDIT host-atomics pattern).
- **Needed, not a diagnostic.** `rt_em_calib` sets the approximate families' scale from it (13.4), and
  `rt_hit_emission` skips an approximate family until it is calibrated. So the check stays on, but cheaper.
- **Change (core and shader only, no ABI).** (1) The counters go to `g_dev` (device-local, core/emissive.cpp):
  per workgroup in groupshared, per record one lane per distinct record and wave (64-bit sums as two 32-bit halves,
  no shaderInt64); the shader's `flush` entry then adds the non-zero words into `emStats` with plain loads and
  stores. The host's layout and reading are unchanged. (2) `rt_em_check_slices` (16): one interleaved 4x4 slice of
  the pixels every `rt_em_check_frames / slices` frames (every frame by default), so every pixel is still checked
  once per 16 frames and no frame traces them all; 1 is the old full-screen run. The calibration's gates count full
  checks: `em::calib_checks` / `calib_px_per_check` (the core scales the host's run counts by the slices). Views
  (rt_debug 16-20) stay full-screen. (3) Its own timer and line, per rt_timing_frames: `[em] check GPU cost (N
  frames): R runs (V views), X ms per run median (check pass, max, timed), Y ms/frame; P pixels per run; schedule`.
- **Expected.** Per run 1/16 of the old ray work and no PCIe atomics: well under 1 ms; per frame the same ray work
  as before on average, without the 1-in-16 spike.
- **Live gate (em_forge `ef1-af-em-fg2`, phases check-off / check-full / check-sliced, 12 s each).** The `[em]
  check GPU cost` lines give the run and frame cost of both schedules; `gi: GPU cost` mean in check-sliced within
  the check-off spread plus that ms/frame; approx families still calibrated (`scale` != 1.000 in `[em] ours/game
  per family`). Note: in that list and in `[em] projected check`, "frames" and bright px per frame are now per run
  (1/16 of the pixels); the calibration rule itself is scaled.
