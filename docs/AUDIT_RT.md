# RT audit: assumptions vs. what the engine provides (2026-10-26)

Scope: every RT/GI/RR/FG input on `main` (`be8d47a`). The question asked of each item:
does the code assume, hardcode or approximate something the game provides, and if so,
does the assumption hold?

**Evidence used.**
- Capture `captures/20261026-094651`: `report.md`, `dumps.json`, `img/`, and the
  spirv-cross shaders in its `shaders/` folder.
- The numbers already measured in `docs/TRACK_D_RT.md` section 9.
- No new game run was made for this audit.

Shader references such as `f469db78:1428` are line numbers in
`captures/20261026-094651/shaders/<hash>.glsl`. Items already fixed in
`docs/QUALITY.md` are not repeated unless the fix itself is wrong.

**Verdicts.**
- **OK**: the assumption holds against the engine data.
- **WRONG**: contradicted by the engine data (the evidence is given).
- **UNVERIFIED**: could not be checked without a run or a scene that has the case.
- **IMPROVABLE**: holds, but the engine offers something better, or the approximation
  costs image quality.

## 1. Table

| Part | Assumption (file:line) | What the engine actually provides (evidence) | Verdict | IQ impact | Concrete fix |
|---|---|---|---|---|---|
| 4 Sun | `rt_sun_radius` = 0.0023 rad, described as "BG3's 0.263° diameter" (`core/gi.cpp:597`; QUALITY fix 5). The shader treats the value as the cone half-angle (`rt_shadow.hlsl:46`, `r = sqrt(u1)·tan(radius)`). | The game's sun specular clamps R into a cone around L: `mix(R, normalize(L·0.99998945 + perp·0.00459006), dot(L,R) < 0.99998945)` (`f469db78:1428`; the same in `118833ff:3116`). That is a disc of **half-angle** asin(0.00459) = 0.263° (the real sun's radius is 0.267°). QUALITY R1 and TRACK_D 8.5 also say 0.00459 is the half-angle. | **WRONG** → **FIXED** (§4 #1) | medium. Penumbrae are half as wide as the game's own sun highlight implies (4.6 cm instead of 9.2 cm for a caster 10 m away), so shadow and specular disagree. | Default `rt_sun_radius=0.00459`. Correct the QUALITY fix-5 text. |
| 4 Sun | Pixels with N·L ≤ 0 get `vis = 0`, and the mask is multiplied by it (`rt_shadow.hlsl:37,78`). | The translucency permutations use the sun mask on back-lit pixels too. The transmitted sun is `profile·T(thickness)·_2997·sun·saturate((0.2588 − N·L)·0.631)`, with `_2997` = mask b7 × cloud shadows (`118833ff:1800-1838, 1918-1920`). It is non-zero only for N·L < 0.259, which includes every back-facing pixel. RT3.w ≠ 0 (translucent) on 0.10% of pixels in the foundry (`d117`). | **FIXED** (§4 #2; was WRONG: the effect is bounded by where the game's mask is lit on back faces; not measured) | medium outdoors (back-lit leaves, cloth, ears lose their sun transmission); low in the foundry | For N·L ≤ 0, leave the mask untouched, or trace from `P − N·bias` towards the sun while ignoring the surface's own triangle. |
| 4 Sun | Origin offset `P + N_shading · 0.002 · dist`, TMin 0 (`rt_shadow.hlsl:38`; also used by GI and D-LS). | The engine has no geometric normal at the pixel, but the TLAS triangle or depth derivatives give one. The QUALITY Q10 bias part is still open. | IMPROVABLE → **FIXED** (§4 #6) | medium: 4 cm of offset at 20 m loses the contact shadows of small props | Offset `max(k·pixel footprint, ε·|P|)` along the geometric normal (cross product of the depth derivatives). Keep the shading normal for the N·L test only. |
| 4 Sun | `mask *= vis` (multiply) | The cascades and the TLAS see the same on-screen casters, so the two signals are correlated. With 1-spp binary `vis`, min(m, v) is the same as m·v. After RR, the result is the product of two soft penumbrae. | IMPROVABLE | low | Replace inside TLAS coverage once cascade casters are captured (TRACK_D 8.5). |
| 4 Sun | Sun direction `cam.v0` = set0 b1 `_m0`; `rt_tmax` 300 | The game uses `_26._m0` for the sun BRDF (`f469db78:1424`). Casters beyond 300 m still come from the cascades. | OK | | |
| 1 Geometry | World position solved from the x, y, w rows of `_m2` in float32 (`xfb_to_world.hlsl`); camera-relative soup; identity TLAS. | The space is the same as the lighting's `ray(uv)·depth` (`f469db78:346`). Primary-ray check: 99.09% within 0.5% (TRACK_D 7.1). Precision is camera-relative. Jitter is in `_m2` and is solved out exactly. | OK | | |
| 1 Geometry | `rt_alpha=1` captures the **depth prepass**, not the G-buffer (`geocapture.cpp:1079-1094`). | Coverage is equal: behind 0.007%, miss about 0. Skinned, wind and terrain geometry is in by construction (XFB). Forward/transparent geometry (water, glass, unit 125) and off-screen or cut-away roofs are missing (known). | OK (known gaps) | | |
| 1 Geometry | Alpha region of `rt_alpha_mb` = 64 MiB (`geocapture.cpp:320`). When it is full, an alpha-tested draw silently falls back to the **opaque** region (`geocapture.cpp:1085-1094`). `kTextures` = 256 (`alphatest.cpp:31`); a full view list also leaves a draw opaque. | The foundry, an interior with no foliage, already uses 0.83–0.93M alpha triangles × 72 B = 60–67 MB of the 67.1 MB region (TRACK_D 7.1). | **FIXED** (§4 #5; the menu foliage scene overflowed) | **high** outdoors: foliage cards become solid, giving black blobs in sun shadows and over-occlusion in GI | Size the region from the previous frame's alpha triangle count × 1.5 (or 25% of `rt_xfb_mb`). Log overflow per frame. Raise `kTextures` (descriptor indexing allows thousands). |
| 1 Geometry | `skip = 4` (indirect draws, strips, fans) is dropped from the RT soup and **not counted** in the report (`geocapture.cpp:1079`, `:714-717` count only skip 1-3). | All 835 foundry draws are direct lists. Other scenes are unknown. | **FIXED** (§4 #10: counted, logged, strips captured; none seen) | medium if any scene uses them: geometry missing without a trace in the logs | Count and log skip 4 per frame. Capture strips with an exact upper bound and degenerate padding. |
| 1 Geometry | Refit when (pipeline, count, instances) match (`geocapture.cpp:1098`; `rtscene.cpp:577`) | Correct by construction for camera translation (world axes) and for skinning. A GPU-culled instance reorder with the same count makes the BVH very loose, but only costs time. | OK (IQ) | | Optional: hash the first instance-stream bytes. |
| 1 Alpha | Cut-off taken from the decompiled `discard` constant (`alpha_fs.txt`), tested as `alpha >= cut` (`rt_common.hlsli:310`) | These are the game's own constants (e.g. `0d91554d:119`: `alpha·_252 − 0.2222 < 0`). The fade factor `_252` (camera cut-away distance fade, `0d91554d:113`) is ignored, which keeps faded walls solid. That is right for shadows, since the cascades see them too. | OK | | |
| 1 Alpha | vt/tex mip from the ray cone, isotropic (`rt_common.hlsli:240-270`) | The game uses screen gradients plus an anisotropy clamp. For secondary rays the cone footprint is the correct analogue. | OK | | |
| 2 Camera | `_m*` meanings: lighting set0 b1 (`_m0` sun direction, `_m6` normal rotation, `_m7` frustum rays), G-buffer set0 b2 (`_m0` view, `_m1` jittered infinite reversed-Z, `_m2` camera-relative VP, `_m4` depth scale, `_m6` camera position) | They match the declarations and uses in `f469db78:153-205, 342, 346, 361`. `to_lighting_space` reproduces `n * mat3(_m6[0..2])` exactly (row-major columns). | OK | | |
| 2 Camera | Linear depth to position: `ray_at(uv)·d` (`gi_common.hlsli:111`) | This is the game's own formula (`f469db78:346`). Rays and depth share the far scale. | OK | | |
| 2 Camera | Docs: far = 1500 (TRACK_D 1) vs 40000 (FRAME_MAP, TRACK_B) | `_m4` varies by scene. The code reads it live. | OK (doc inconsistency) | | Note in the docs that `_m4` varies by scene. |
| 3 G-buffer | Stereographic decode, F0 = mix(0.16·y², base, metal), roughness RT2.x, channel bits 4-6 | Identical to `f469db78:321-345`. Base colour RGBA8 UNORM is used as linear (no sRGB decode in the game), so the guides are consistent. | OK | | |
| 3 G-buffer | RT2.a is treated as "flags, bits 4-6 channel" only; RT3 as "almost all zero: emissive or sparse flags" (FRAME_MAP) | RT2.a bits 0-2 are the **material class**: 3 = SSS (`d1669194:61-…`, `6f535075:61`), 1 = skipped by the lighting (`702bda11`). RT3 is the per-class parameter target: SSS profile index, scale and thickness with a translucency flag in .w (`118833ff:367-380`); the anisotropic tangent, octahedral 12-bit (`4265b844:388-395`); the cloth sheen colour (`95af6e4f`). Foundry: RT3 non-zero on 0.20%, class 3 on 0.004% (`d116`, `d117`). | **WRONG** → **FIXED** (§4 #3, FRAME_MAP) | see the D-LS and GI rows | Document the classes. Decode RT2.a&7 and RT3 in `gi_common.hlsli`. |
| 5 GI | Composite `diffuse += gi·albedo·(1−metal)` on every pixel (`gi_composite.hlsl:19`) | On SSS pixels the lighting writes diffuse with the colour forced to 1: `mix(base, 1, isSSS && set0b0._m4≠0)·(1−metal)` (`118833ff:370`, `34a007cc:405`, `8b781b6e:374`). The SSS pass then writes `RT1.rgb × blurred` back into b10 (`d1669194:359`, unit 122 → i11683). With `_m4 = 0` the game itself would square the albedo, so `_m4 ≠ 0` whenever the SSS passes run, and they ran in this frame. | **WRONG** → **FIXED** (§4 #3) | medium on faces in close-ups and dialogue (bounce × albedo², too dark and saturated); negligible in the foundry wide shot | On RT2.a&7==3 pixels, add `gi·(1−metal)` without albedo. |
| 5 GI | `prevRadiance = diffuse + specular` (`gi_composite.hlsl:28`) | It is pre-exposure, pre-fog and pre-UI, in the units of b10/b11: correct. But b10 also contains the atmosphere in-scatter `_2979·_m19·10·b13[0]` (`f469db78:1623`) and view-dependent specular. On SSS pixels it is irradiance without albedo (white skin bounce). The emissive/FX target i11680 (`d109`: max 5.9, written before the lighting, added in compose) is missing. | IMPROVABLE (QUALITY Q8 is still open) | low-medium | Diffuse only (or + spec·r²), × albedo on SSS pixels, + i11680 (excluding the sky). |
| 5 GI | Bounce read at the MV-reprojected pixel, accepted on the *current* depth only (`gi_common.hlsli:140-146`, `gi_trace_rt.hlsl:63`) | `prevRadiance.a` is always 1; the depth half of Q1 was not done. The MV is fetched by nearest neighbour. | IMPROVABLE | low-medium (wrong-surface bounce at silhouettes in motion) | Store previous linear depth in `.a` and reject on mismatch. |
| 5 GI | AO fold `ao.x = lerp(ao.x, vis, 1)` replaces SSAO (`gi_ao_apply.hlsl:15`) | The lighting uses AO for the ambient (`f469db78:1451-1452`, `× AO` and Lagarde specOcc) **and** multiplies direct light by `mix(1, AO, m0.y)`, with m0.y = 0.5 (TRACK_D 9.1). In RR mode (`gi.cpp:602-605`: history 1, 1 ray, no filter) b6.x is raw 1-spp visibility, so **direct sun and torch light get up to 50% per-pixel noise**, on top of the RT shadows that already resolve the contacts. | IMPROVABLE → **FIXED** (§4 #4, `ao_direct=1`) | **medium-high**: RR has to denoise direct lighting that was noise-free, which blurs it, and contacts are darkened twice for direct light | Give direct light back its own factor. In the D-LS post pass, add `c_i·(mix(1,SSAO,.5) − mix(1,vis,.5))` for the local lights and the sun (the recompute already has c_i; the sun term is `f469db78:1422-1452`). Or use EN4/EN5 so that b6.x stays the game's SSAO. |
| 5 GI | Bounce energy `albedo·(1−metal)·mean(L·w)` | The game's ambient is `E·diff_col·DFG.z·b13·100·AO`. DFG.z (LUT b38) is not applied (QUALITY Q9, open). | IMPROVABLE | low | × `b38(NoV, r).z` |
| 5 GI | `gi_rt_fallback=0`: hidden or off-screen hits occlude with no bounce | 11.7% + 2.2% of the hits (TRACK_D 7.2) | IMPROVABLE (known) | low-medium (slight darkening) | Radiance cache (D-GI2). |
| 6 RT AO | Linear seed `hash(x·7307 + y·3323 + f·15013)` (`rt_ao.hlsl:29`, `gi_contact.hlsl:24`) | QUALITY Q5 says pcg3d; only the GI and sun passes were switched | IMPROVABLE | low (`rt_ao` is off by default) | `pixel_seed()`. |
| 7 D-LS | The recompute replicates `f469db78` for every pixel (`bg3_lights.hlsli:208-250`) | The frame dispatches **14 lighting permutations** (`report.md` units 104-117). Several use a different BRDF: cloth sheen (`95af6e4f`: Charlie D, Ashikhmin V, RT3 colour), anisotropic GGX (`4265b844`, RT3 tangent), SSS dual lobe with diffuse colour 1 plus local-light transmission (`118833ff`, `34a007cc`, `8b781b6e`), and class 1 skipped entirely (`702bda11`: `if (cls == 1) break`). | **WRONG** → **FIXED** (§4 #3; projector lights still not recomputed) | medium on characters (clothing, hair, skin): skin shadows only remove albedo × the light (30–50% too weak after the SSS pass); class-1 pixels lose light the game never added | Per pixel: if RT2.a&7 ≠ 0 or RT3 ≠ 0, either replicate the class's term (sheen, aniso, diffuse colour 1 on SSS) or leave the pixel alone. Exclude those pixels from the validation fit. |
| 7 D-LS | The "7% residual" (fit `game = 1.07·rec + 0.0134`) is left unexplained (TRACK_D 9.3) | **Model.** With AO = 0 and the sun mask = 0, the game's output is `b10 = T·(Σ_f469 + Δ_class)·0.5 + S` and `b11 = T·Σ_spec·0.5` (`f469db78:1451, 1622-1623`).<br>(a) `S` = atmosphere in-scatter, added to diffuse, independent of the lights. It explains the intercept (0.0134 lit, 0.018 unlit). A spatially varying S with cov(S, rec) = 0.07·var(rec) gives the slope as well (OLS: k = 1 + cov/var; this implies a lit-pixel mean S of 0.0141).<br>(b) `Δ_class` (row above) is ≥ 0 on SSS and translucent pixels, but covers only about 0.2% of pixels here, so it cannot carry 7% on its own.<br>(c) `T ≤ 1` pulls the slope below 1, not above. | UNVERIFIED (the model fits the numbers; not confirmed) | low (validation only) | Restrict the fit to standard-class pixels. Fit with S measured per pixel: replicate `_2979` (`f469db78:1458-1615`, set 5 LUTs), or set `_m19`-weighted in-scatter to zero by a prep-pass toggle if one becomes possible. The fit should then come out at k ≈ 1 and r ≈ 0. |
| 7 D-LS | `ls_source_radius` = 0.05 (`gi.cpp:622`) | No source size exists in the engine. The falloff is `pow(sat(1−(d²/r²)²), m4)·(1/max(d², 1e-4) + |m5|)`, or `(4/r² + |m5|)` (`f469db78:406-416`). m5 is an additive bias, not a radius. m9 is not read by `f469db78`. | UNVERIFIED (an artistic value) | low-medium | Make it relative: `ls_source_radius·range`, or per-model (m5 < 0 lights are "flat" fill lights and could be larger). Tune it in the torch-lit scene. |
| 7 D-LS | `ls_clear` = 0.2: rays stop 0.2 short of the light (`rt_local_shadows.hlsl:73`) | This is an empirical fixture skip. It also removes every occluder within 20 cm of the light, and every shadow for surfaces closer than 0.2 to it. | IMPROVABLE | low | `max(0.05, 0.05·dist)`, or skip the first hit only if it is within the light's range and back-facing to the light. |
| 7 D-LS | Lights with a game shadow map are skipped (`ls_shadowed=0`) | No such light in the foundry (0.00/px) | UNVERIFIED | | A/B in a scene that has them. |
| 7 D-LS | Projector lights are not shadowed | 1.02 listed per pixel, as many as half the evaluated points and spots (TRACK_D 9.1). Their term is fully in `f469db78:736-966`: box-space window with `_304[]` polynomial falloff, cookie b45, × b13[0] if `m1&1`. Their own shadow uses a **different** depth compare, `((z − m5.y)/m5.x)/m6` (`:839`), against perspective `(m5.y/m6)/(z − m5.x)` for points and spots (`:480`). | IMPROVABLE | medium (leaks from textured fills remain) | Add a third loop with the same exact-delta pattern. |
| 7 D-LS | The delta is not multiplied by the aerial transmittance T (`rt_local_shadows.hlsl:214`) | `b10 = (direct + ambient)·T + S`, `b11 = spec·T` (`f469db78:1622-1623`) | IMPROVABLE | low (T ≈ 1 indoors; slight over-removal at distance outdoors) | Multiply the delta by T, or fade it out with distance. |
| 7 D-LS | Random stream key `li & 1023` (`rt_local_shadows.hlsl:38`) | Up to 4096 lights; li and li+1024 share a stream | IMPROVABLE | low | Use a 3rd pcg dimension: `pcg3d(fp, frame) ^ hash(li)`. |
| 8 RR | RR colour = the game's SR input (i11340) | That image is **after** god rays (`a7466e4e`), exposure (`2cdd9f08`), bloom (`0463a527`: `+0.2·bloom`) and a **3D-LUT colour grade in PQ space** with an HSV adjust (`0592680b`, units 133-151). The grade is not identity here: mean 0.0907 → 0.0656, max 27.5 → 42 (`d160` → `d161`). It is pre-UI and pre-tonemap (tonemap `d25b8086`, unit 153), but not linear scene radiance. The RR guide 3.1 says to place RR "before any effects" of post. | IMPROVABLE | **high** for every RR-denoised RT effect: a non-linear grade before the temporal accumulation biases the 1-spp mean (E[f(x)] ≠ f(E[x])), and albedo demodulation no longer matches | Give RR i11702 (before the grade) as its colour, and run `0592680b`'s grade ourselves on the 3840x1600 RR output (46 lines: 4 UBO floats + a 32³ LUT, i795). Then the unit-124 copy also becomes a valid ColorBeforeTransparency. |
| 8 RR | MV scale and sign: the game's (−1, −1), render pixels, unjittered | Taken unchanged from the game's SR block. The GI reprojection works with the same sign (`-Move`). | OK | | |
| 8 RR | Depth: HW D24, inverted, `Use_HW_Depth` (`ngx.cpp:979`) | Accepted by RR. Linear R32F also exists, but brings no gain. | OK | | |
| 8 RR | Roughness guide = raw RT2.x (`rr_guides.hlsl:88`) | The shading uses `sqrt(max(0.09, r)² + min(var, 0.18))` (`f469db78:345, 355`). The RR guide asks for linear (perceptual) roughness. | IMPROVABLE | low (mirror-like pixels below 0.09) | `max(0.09, r)`, plus the specular AA term. |
| 8 RR | Specular albedo = EnvBRDFApprox2 (`rr_guides.hlsl:86`) | The game's own split-sum LUT b38 (i86) gives `F0·A + B` exactly (`f469db78:986, 993`) | IMPROVABLE | low | Snapshot b38 and sample it at (max(NoV, 0.00195), a). |
| 8 RR | Diffuse albedo `base·(1−metal)`; sky 0.5 | It matches the RR colour after SSS (albedo re-applied). The sky value is the guide's recommendation. | OK | | |
| 8 RR | No DiffuseHitDistance, transparency, SSS or ColorBeforeTransparency guides (`ngx.cpp:1116-1140`) | The GI ray `t` exists. The SSS passes (units 121-122) and the colour copy before the forward pass (unit 124) are in the frame (QUALITY Q12). | IMPROVABLE | medium (1-spp GI convergence; smearing on water and particles) | Write `t` (miss: 65504) from `gi_trace_rt`. Add the unit-124 copy after the colour-input fix above. |
| 8 RR | AutoExposure dropped, pre-exposure 1, no exposure texture (`ngx.cpp:974`) | Exposure is already applied before the input (`2cdd9f08`) | OK | | |
| 9 DLSS-G | `cameraNear/Far` = UBO `_m3/_m4` (`streamline.cpp:836-837`) | The projection is infinite reversed-Z (`_m1`). `_m4` is the scene depth scale (1500 / 40000), not a projection far plane. If Streamline linearises depth with a finite far plane, the error is (f−z)/(f−n): about 1% at 16 m with f = 1500. | UNVERIFIED | low | Set `cameraFar` very large (or use infinite-far handling, if SL supports it). Check the occlusion quality. |
| 9 DLSS-G | Hudless = the tonemapped HDR10 image before the UI composite; UI = RGBA8; MVs and depth as for DLSS | Matches units 153-166 and `046c6949` | OK | | |
| 9 DLSS-G | `clipToPrevClip` from the camera-relative VPs, unjittered, with T(cp − pp); jitter from P[2] | Correct algebra (row-vector form) | OK (the jitter sign is still unchecked, TRACK_B) | low | |

## 2. Prioritised fix list

1. **Feed RR an ungraded colour** (part 8, high). Use i11702 as the RR input and apply
   the `0592680b` grade (HSV + PQ 32³ LUT) to the RR output at output resolution.
   - This removes the non-linear bias on every 1-spp RT signal.
   - It also makes the unit-124 ColorBeforeTransparency guide usable.
2. **Size the alpha XFB region from demand** (part 1, high outdoors). Log overflow and
   "views full", and raise `kTextures`.
   - The foundry already fills 90–100% of the 64 MiB region.
   - On overflow, foliage silently turns opaque.
3. **Give direct light back its own AO factor** (part 5, medium-high).
   - Today the 1-spp RT visibility in b6.x also multiplies direct sun and local light
     (`mix(1, AO, 0.5)`), which injects noise into direct light under RR and double
     counts contacts already resolved by RT shadows.
   - Compensate in the D-LS post pass for local lights and the sun, or keep SSAO in
     b6.x and move the RT visibility to an ambient delta (EN4).
4. **`rt_sun_radius` = 0.00459** (part 4, medium). It is the half-angle of the game's own
   sun disc. One-line default change, plus the QUALITY text.
5. **Material classes** (parts 3, 5, 7, medium).
   - Decode RT2.a&7 and RT3.
   - D-LS: skip or replicate the sheen, aniso and SSS classes, and use diffuse colour 1
     on SSS.
   - GI composite: no albedo on SSS pixels.
   - Fix FRAME_MAP.
6. **Back-facing sun pixels** (part 4, medium outdoors). Do not zero the mask for
   N·L ≤ 0; the translucent and SSS sun transmission reads it.
7. **Projector-light shadows** (part 7, medium; 1 light per pixel). Use the same delta
   pattern with the projector's own falloff, cookie and depth formula.
8. **Geometric-normal ray offset** (part 4, medium). This is the remainder of Q10, and it
   is shared by the sun, GI and D-LS rays.
9. **RR DiffuseHitDistance and the guides of QUALITY Q12** (part 8, medium).
10. **Count and log skip-4 draws** (part 1, unverified medium). Capture strips if any
    scene uses them.
11. **Low**:
    - the `prevRadiance` source (diffuse only, emissive i11680, depth in `.a` for
      reprojection rejection);
    - DFG.z in the composite;
    - the roughness guide clamp and spec AA;
    - the specular albedo from LUT b38;
    - D-LS × T and `ls_clear` scaling;
    - `ls_source_radius` relative to range;
    - pcg seeding in `rt_ao` and `gi_contact`;
    - the D-LS stream key;
    - the DLSS-G far plane;
    - the 7% fit restricted to standard-class pixels, with the in-scatter replicated.

## 3. Not checked / needs a run

- The runtime value of set0 b0 `_m4` (the SSS diffuse-colour switch). The argument for
  `_m4 ≠ 0` is indirect (the SSS composite would otherwise square the albedo).
- How often the alpha region overflows outdoors and in foliage scenes. How many draws
  are skip 4 there.
- The game's sun mask on back-facing translucent pixels (it bounds the effect of the
  N·L ≤ 0 issue).
- Whether the `0592680b` grade parameters vary by area (they do change the image here).

Answered by the fix batch below (2026-10-26):
- `_m4` is **1** in gameplay (0 on the loading screen), read on the GPU (`ls stats`, raw counter 54).
- The alpha region: the foundry wants 57-62 MB, the loading-screen foliage scene 80 MB (it
  overflowed the fixed 64 MB on its first frame: 51 draws went opaque), the docks 14 MB.
  No skip-4 draw (indirect, strip or fan) in the foundry, the docks or the loading screen.
- The game's sun mask on back-lit transmitting pixels (N·L < 0.2588): lit on 66% of them at
  the docks, 18% in the foliage scene, 0-9% in the foundry.

## 4. Fix batch (2026-10-26, branch of `e285602`)

Items 1, 2, 3 (D-LS and GI parts), 4, 5, 6 (QUALITY Q10) and 10 of the list in section 2,
and the related table rows. Each has a live ini switch (A/B) and is on by default.
Measured in the Steel Watch Foundry (static) and at the docks (the `-Move` pan's end, outdoors,
sun), RTX 5090, 2560x1067. Images in `docs/img/rtfix*`.

| # | Item | Fix | Switch | Evidence |
|---|---|---|---|---|
| 1 | Sun angular radius | `rt_sun_radius` default **0.00459** rad. Confirmed half-angle: `f469db78:1428` builds `L·0.99998945 + perp·0.0045900648` (cos² + sin² = 1), a cone of half-angle 0.263°; the shader samples a cone of half-angle `rt_sun_radius`. Comments and QUALITY fix 5 corrected. | `rt_sun_radius` | |
| 2 | Back-lit translucency | `rt_shadow.hlsl` decodes the class (RT2.a & 7). Skin (3), thin translucency (4) and hair (5) are traced also for N·L ≤ 0, the ray leaving on the light's side; other back faces leave the mask untouched (they never read it). | `rt_backlit` | Docks: transmitting back-lit pixels are 1.07% of the screen, the game's mask is lit on **66.4%** of them (all zeroed before), our trace on 56.8% (game lit / traced occluded 14.4%: casters the cascades miss). Foliage menu scene: 20.6% of px, 17.8% / 21.3%. `rtfix2_backlit_docks_crop0.png`: a back-lit bush gets its transmitted sun back. |
| 3 | Material classes | Classes 0-7 documented (FRAME_MAP). RT3 (set 1 b4) and the SSS profiles (b28) are snapshotted from the material permutations (they are not bound to `f469db78`), set 0 b0 for `_m4`. **D-LS**: each light with the class's exact term (`rt/bg3_materials.hlsli`: skin diffuse colour 1 + dual GGX + transmission, translucency transmission, anisotropic D, Marschner hair with its self-shadow, sheen and GGX/sheen blend; unlit pixels untouched); back-lit transmission traced from the back side. **GI composite**: bounce × the class's diffuse colour (white on skin, as the SSS pass multiplies the albedo in later: no more albedo²; raw albedo on hair and cloth; nothing on unlit), and `prevRadiance` × albedo on white skin. Validation fit restricted to class 0. | `mat_classes` | Foundry, `ls_debug=4`: non-standard pixels 0.21% (character); recompute above the game **0.0000** (never), mean recomputed 0.0104 vs game 0.0288 (the same additive in-scatter residual as class 0). Class-0 fit unchanged (1.07 × + 0.0134). Docks: 21% non-standard px (sails, ropes: cloth), 1.4% translucent. `rtfix3_materials_*`. |
| 4 | AO feeding direct light | **(b), exact**: the D-LS pass removes the game's `mix(1, AO, m0.y)` from every light whose shadow is ray traced (its local lights, and the sun when `rt_shadows` folded it), the sun being recomputed exactly (`rt/bg3_sun.hlsli`: colour or atmosphere LUT, both cloud-shadow layers, the class BRDFs, the disc point). Ambient keeps the RT visibility (its own occlusion term). Option 2 keeps the game's own factor with its own SSAO (saved in `gAux.x` before the fold). See the option comparison below. | `ao_direct` (1 default, 2 game look, 0 old) | Light given back: 1.9% of the locally lit luminance in the foundry, 0.0037/px (~1.1%) at the docks; `rtfix4_aodirect_*`, `rtfix4_aodirect_givenback_x4_foundry_on.png` (where it acts: contacts near lamps). Sun recompute validated at the docks (`ls_debug=7`: AO 0, local lights none): recomputed 0.170 vs game 0.188 per px, the difference being the in-scatter (0.013-0.018 measured indoors); the recompute exceeds the game on 0.75% of px (3.8% of the luminance), not explained (left). Cost +0.045 ms. |
| 5 | Alpha region / textures | Region sized from the previous frame's demand ×1.5 (grow at once, shrink below 1/3), min `rt_alpha_mb`, max `rt_alpha_max_pct` (60%) of the 384 MB buffer and what the opaque soup needs; a new split forces a BLAS rebuild. `kTextures` 256 → 4096, tile caches 4 → 16, records 2048 → 8192 (descriptor writes limited to the slots in use). Overflow, views/records full and opaque drops are logged at once as `[geo]/[alpha] WARNING`, and per 300 frames. | `rt_alpha_auto`, `rt_alpha_max_pct` | Region 120 MB after the loading screen's 80 MB peak; 0 overflow afterwards in all scenes; foundry 299 alpha draws/frame, docks 47 (14 textures). |
| 6 | Ray origins (Q10) | `gi/rt_origin.hlsl`: per pixel a short ray (±1% of the distance) finds the TLAS triangle under the depth sample; its geometric normal (from the soups, set 1 b6 added for the opaque one) and exact distance go into `rtOrigin`. Every pass (sun, RT AO, RT GI, D-LS) starts rays at that surface point pushed off by Wächter & Binder's ULP offset along the geometric normal, on the ray's side. Shadow-terminator pixels (shading and geometric normals disagree about the side) and pixels without a matching triangle use 2 pixel footprints along the shading / depth-derivative normal. Non-finite origins are never traced. | `rt_origin` | TLAS surface found under 99.2-99.6% of foundry px, 83% at the docks. `rtfix6_origin_sunvis_foundry_*` (the traced sun visibility, `rt_debug=1`): thin contact shadows of railings, cables and pipe feet appear, flat lit floors stay clean (no acne); `rtfix6_origin_lsvis_foundry_*` the same for D-LS. Cost +0.15-0.2 ms (pre pass) + 0.05 ms (D-LS). |
| 10 | Dropped draws | Indirect draws and non-sizable topologies are counted per frame and logged; triangle strips and fans are captured (count − 2 per instance: non-indexed, or indexed without primitive restart). | `rt_strips` | 0 dropped in the three scenes. |

**Fix 4, the options compared.**
- (a) A blurred or denoised visibility in b6: it would be a pre-filter under RR for the
  ambient (QUALITY R2), and it still darkens contacts twice where the RT shadows already
  occlude. Rejected.
- (b) Exact compensation (chosen, `ao_direct=1`): physically the right composition, since the
  ray-traced shadow is the occlusion of that light. Direct light carries no AO noise, and the
  ambient keeps the RT visibility. It needs D-LS and the sun recompute, which are exact
  replicas of `f469db78` validated above.
- (c) The game's SSAO for direct light and our visibility for the ambient only
  (`ao_direct=2`, same machinery): it keeps the original look and is noise-free, but the
  contacts the RT shadows resolve are still darkened by SSAO. Kept as an option.
- (d) A lighting-shader patch (EN5) would avoid the recompute, but it touches 14
  permutations. Not needed now that the recompute is exact.
- Not compensated: projector lights (not recomputed, they keep `mix(1, AO, 0.5)` of the RT
  visibility), lights left to the game's atlas (`ls_shadowed=0`) or below `ls_min_radius`,
  and thickness-profile transmission (`m2.z == 1`, from the cascades).

**Found on the way.**
- **maxPushDescriptors is 32.** With the new bindings, the D-LS push layout reached 35-42
  descriptors (`VUID-VkDescriptorSetLayoutCreateInfo-flags-00281`). The descriptors past
  the limit were garbage, which gave GPU page faults (`nvlddmkm` 153) when D-LS read the SSS
  profiles or the origins. Now:
  - the D-LS layout pushes only the 26 bindings its shader uses;
  - the sun's inputs go into an ordinary set 2.
  Keep the D-LS push layout ≤ 32 when adding bindings.
- `layer/tracer.cpp` built the request path as `"\trace.request"`, which is a tab
  character, so `trace.request` never triggered a trace. Fixed: manual and scripted traces
  work again, and `-Move` A/B at the docks uses them.
- The soups are now read with bounds checks (`rt_tri_normal`), and the SSS profile index is
  clamped to the list.

**Not done / not verified.**
- D-LS × the aerial transmittance T (deltas outdoors at distance).
- Hair under lights that have a shadow map: the game replaces the hair self-shadow by the
  atlas term (left to the game).
- The 0.75% of docks pixels where the sun recompute exceeds the game.
- A foliage scene in gameplay: the back-lit numbers for heavy foliage come from the 3D menu
  scene, and the docks have a few bushes.
- A skin close-up: the skin terms are validated only on a few hundred pixels (the
  character in the foundry).
- The D-LS base cost rose from 0.345 ms (the doc) to about 0.48 ms with every fix off in
  the same run. That is the generic per-class evaluation (register pressure). The cost is
  within budget, but not optimised.

**Verification (after merging main `1046622`, D-RF included).**
- `-Validation`, foundry: the message IDs and counts are identical to the baseline captured
  on `e285602` (`09600`, `10788`, `StorageImage-FormatMismatch`, `OutputNotWritten`,
  `CopyBuffer-00118`, `08737`, the headless swapchain message).
- No non-finite value in any dump of the A/B traces.
- `-Move` to the docks with every fix on: no faults and no `nvlddmkm` events (since the
  push-layout fix).
- `-Background -RealPresent fg=2`: DLSS-G status 0x0 and 0 API errors. It presented 1 frame
  per app frame, and exactly the same with all four fixes off, so that is the background
  mode, not the fixes. 2x generation was not re-confirmed in front.
- GPU cost, foundry static, RTX 5090, medians of 300 frames:

  | | all fixes off | all on |
  |---|---|---|
  | RT pre passes (`rt_origin` + sun + GI) | 0.29–0.35 ms | 0.50 ms |
  | GI trace | 0.149 ms | 0.163 ms |
  | D-LS | 0.477 ms | 0.559 ms |

  At the docks the RT pre passes take 0.43 ms with `rt_origin` (0.275 ms without), and D-LS
  0.136 ms. The parts: `rt_origin` costs +0.15–0.2 ms, the sun recompute +0.045 ms, and the
  material classes about 0.
