# Track C: screen-space GI (v0)

## Where it runs
BG3's direct lighting is a tiled compute pass: about 15 `dispatch_indirect`
permutations of one shader family (e.g. `f469db78`). They write two
R11G11B10F storage images, both in GENERAL layout:

| Binding | Contents |
|---|---|
| `set1/b10` | diffuse radiance; the IBL ambient is already included as `_1687*_2104·AO` |
| `set1/b11` | specular radiance |

The subsurface-scattering blur passes and fog compose read them afterwards, from a
**different command buffer**.

GI is added into the diffuse image at the end of the lighting's command buffer
(`on_cmd_end`), or at the first render pass after the lighting if that comes first
(`on_pass_begin`). This means:
- SSS blur still applies to the bounce light on skin;
- fog, transparency, post and DLSS all see the result.

## How the inputs are found
At each indirect dispatch the core calls `host->query_binding` and recognises the
lighting shader from what is bound. It then reuses the game's own descriptors
directly.

| Binding | Resource |
|---|---|
| `set0/b1` | camera UBO (dynamic). `_m6` is the view-normal transform (row-major); `_m7[3]` are frustum rays. Positions are `ray(uv) * linearDepth` in a camera-relative space. |
| `set0/b4` | viewport UBO: `ab = ((uv - m1) / m0) * 0.5` |
| `set1/b1` | normals, RG16F stereographic (view space) |
| `set1/b2` | albedo |
| `set1/b3` | material: roughness, specular, metal, flags |
| `set1/b5` | R32F linear depth |

Projection back to the screen solves `P = d·(r0 + (r1−r0)a + (r2−r0)b)`. It uses
only the game's own constants, so no matrices are guessed.

## Algorithm (shaders/gi/, hot-reloaded)
1. `gi_trace`: half resolution, with a checkerboard rotation inside each 2x2.
   - Cosine-hemisphere rays (`gi_rays`, default 2), `gi_steps` (default 12) quadratic steps up to `gi_radius·depth`.
   - A hit needs to land within `gi_thickness` relative depth of the surface.
   - On a hit, the ray takes the game's freshly lit diffuse + specular at that pixel: one bounce of direct light.
2. `gi_temporal`: exponential history (`gi_history`, default 0.1) with depth-based rejection. There's no reprojection yet.
3. `gi_composite`: depth-aware 3x3 upsample, then `diffuse += GI · albedo · (1 − metal) · gi_strength`.

## Settings (bg3rtx.ini)
`gi=0/1`, `gi_strength`, `gi_radius`, `gi_thickness`, `gi_rays`, `gi_steps`,
`gi_history`, `gi_debug` (1 = GI irradiance only, 2 = added term ×4), and
`gi_describe=N` (logs the bindings of the first N indirect dispatches).

## Status (2026-10-26, v1)
- v1 does two things. Before the lighting, ray-traced visibility is multiplied into BG3's
  AO target, so the game's own IBL ambient (diffuse and specular) is occluded. After the
  lighting, a one-bounce term from last frame's radiance is added, which also gives
  multi-bounce over frames.
- Verified in game. Contact occlusion is visible under pipes and railings and around
  characters, and the mean frame luminance is about 5% lower than with GI off. No
  non-finite values and no faults, including together with Ray Reconstruction (Track A).
- GPU cost, from timestamps: about 0.18 ms at 2 rays / 12 steps and about 0.28 ms at
  4 rays / 16 steps (the new default). Half resolution, 2560x1067 internal.
- Bugs fixed during the bugfix pause:
  - Ray lengths must scale with the camera distance: the frustum rays carry the
    far-plane scale.
  - The GI must never write non-finite values into game buffers.
  - `dumps.json` wrote `inf`/`nan`, which is not valid JSON.

- Temporal reprojection uses the motion vectors the game passes to DLSS (exposed by the NGX hook,
  `host->dlss_motion_vectors`; their pass leaves them in COLOR_ATTACHMENT layout, so they are
  transitioned around the read). Verified with `auto-run.ps1 -Move` (camera pan): no visible ghosting.
- Sun contact shadows refine the game's screen-space sun shadow mask (`contact_shadows`, `cs_*`).

## Validation pass (2026-10-26)
`auto-run.ps1 -Validation` runs the game under the Khronos validation layer, placed below our layer so
it also checks what we record. Comparing message IDs with GI off and on isolated our errors:
- A stale motion-vector image after a level load (render targets are recreated) made the GI barrier
  an invalid handle. Under validation this faulted inside the validation layer's locks and hung the
  game. Fixed two ways: the NGX hook drops a destroyed MV image, and the GI uses MVs only if the
  velocity pass was seen writing them in the current frame.
- `restore_bindings` rebound sets with too few dynamic offsets (7 required, 5 tracked). The tracker
  now records descriptor set layouts, so dynamic offsets are split exactly. Restore replays the game's
  own bind calls verbatim, and `query_binding` resolves dynamic UBO offsets from the layout.
- Barrier access masks were invalid for their stages; they now use MEMORY_READ/WRITE.
- Remaining: one `Threading-MultipleThreads-Write` (`vkDeviceWaitIdle` vs `vkQueueSubmit`) at
  shutdown, related to headless presentation. It is low priority.

## Hybrid ray-traced trace (`gi_rt=1`, Track D phase 3, off by default)
With `rt=1` the trace can use hardware ray queries against the TLAS
(`shaders/gi/gi_trace_rt.hlsl`); temporal, AO fold and composite are unchanged.
Hits that project on screen with matching depth take last frame's radiance;
hidden or off-screen hits count as occlusion with no bounce (`gi_rt_fallback`).
Trace pass 0.175 ms vs 0.248 ms screen-space (4 rays, half res); see
TRACK_D_ALPHA_GI.md section 7.2 for numbers and comparison images.

## SHaRC radiance cache (`sharc=1`, off by default)
A screen-injected world-space hash grid gives hidden and off-screen GI hits (and glossy
reflection fallbacks) a bounce, and lengthens the GI rays to `sharc_gi_radius` (8 m). Design
and status: DESIGN_SOTA_NEXT.md 2.9 (`core/sharc.cpp`, `shaders/gi/sharc_*.hlsl`).

## Qualified hidden-hit direct bounce (`gi_hit_direct=0`, 2026-10-31)

R3 corrected-contract acquisition: candidate d19ecd7, queue VALID, 580 seconds,
three K16 OFF/ON/OFF bursts, FG active all 48 frames, no NVIDIA 153/dumps/build
drift. 565595 stable pixels; zero negative GI-envelope pixels, 21517 positive.
Previous failure ROI: final mean-low pixels beyond OFF-repeat are 14/12812;
old R2 was 1144/19043. Different sessions/denominators, not a cross-run A/B.
Whole GI median OFF A/ON/OFF B: 2.097/2.288/2.048 ms; not isolated bounce cost.
Broad final-image review, motion and visible gain remain pending. User paused
work. Key OFF; no trial installation. Exact job, results and contact sheets:
docs/evidence/gi-descriptor-r3-20261031/. Raw captures in user-regression-ab.

R2 key-on images are **rejected**: visible new dark stippling despite positive GI
counters. The original dedicated shader was mistakenly dispatched through the
contracted D-LS set0 layout/writes, missing GI output/history/MV bindings and the
SHaRC/rebase camera. A source-only fix supplies one typed GI/native-light contract
for both layout creation and pushed writes (19 descriptors, 20 with physical Hair).
Actual compiled SPIR-V descriptor types/counts/push range are checked against it.
The direct/two-surface experimental reference also uses its own typed 18-slot
layout/write contract, including rebase camera 48; ordinary local shadows retain
their existing contraction. Deep reference variants need fresh image validation.
The key stays off pending a new authorized image A/B; compilation and positive
radiance counters alone do not qualify image quality.

Experimental and off in every preset. Requires native-material startup,
`gi_rt=1` and `gi_rt_fallback=0`. Successful screen and SHaRC hits keep priority.
A qualified fresh native VT hit may replace only the zero-radiance fallback with
`bounce += weight * direct_radiance`, preserving exactly the old `occ += weight`.
The existing primary diffuse colour and GI strength are applied once by the
composite. Direct-only light never replaces full probe or cache radiance.
Unknown material, source, visibility or query registration retains the original
fallback. Misses remain misses; no sky/environment constant is invented.

The declared opaque FresnelBlend model uses actual native point/spot inputs.
It includes directional diffuse and specular scattering along the secondary ray;
the screen-radiance source stays diffuse only (`gi_bounce_spec`: removed 2026-10-31, see
CONSOLIDATION.md C1). This is a
physical directional addition, not a promise of identical diffuse-only semantics.
The current native froxel provider supports hidden points inside the view
frustum. True offscreen lights, sunlight, projector and emissive terms remain
unsupported. ABI 21 registers the exact tuple and returns its query-owned set
before tracing; startup-off material resources still require restart.

Hit lighting diagnosis (2026-10-31, branch hit-lighting, offline). The froxel lists are not
depth-bounded: the cluster build (cs 20aea152, captures/alpha_cache:403/665) tests light spheres
against the froxel planes, with no depth input, so a hidden in-frustum point gets its froxel's full
list. Falloff, cone, channel (native UBO 104, as the G-buffer FS 8995da1e writes RT2.w) and b13[0]
match bg3_light_eval. The hit point is now the triangle point (`native_hit_position`), the weighted
luminance sum is x 8192 (x 256 rounded to 0), and `GI hit direct lights` logs what zeroes the term:
rays traced/lit/occluded, occluded with the first hit within 1 cm or on the hit's own triangle
(self-intersection), empty froxel, channel/range/cone zero, N.L <= 0, unsupported by reason
(S6: 0 supported of 260k qualified; the reason split answers it).

`rt_stats=1` reports candidates, qualified materials, supported direct terms,
nonzero additions and weighted luminance as `GI hit direct`. Cost remains inside
the existing GI trace interval. Live contribution, motion and cost remain
unqualified until same-run A/B. This changes lighting by an accounted addition;
it does not claim unchanged-transport energy preservation or full M4 completion.

S6 "non-finite" alarm of m4p3 (2026-11-01, branch worktree-agent-ad96013fbc66f09f7): not ours.
- The gate printed `nonfinite 8692` for step-gi. That was the sum over all dumps of three traces
  (OFF-a 2917 + step 2865 + OFF-b 2910), all of them in the game's own 2048x2048 D32F depth of
  pass `76433aef` (QA.md "Non-finite": garbage texels +-3.4e38 and NaN, the same with every RT key off).
  The measured scene colour (`5c3fdabc` slot 0) has 0 non-finite values in all 8 S6 traces.
- `tools/m4_promotion_gate.py` now prints the scene colour's own count per trace and every other
  non-finite dump per trace, marked game-owned when an OFF trace has it too (test case added).
- So the -1 to -3% darkening of step-gi / sun / inst is not explained by NaN in the measured buffer.
  By reading, every hit term is finite and >= 0 when its status is OK (hd_scatter, native_hit_sun,
  em_radiance guards), so the bounce sum cannot darken. One hidden path remains: giRaw is RGBA16F and
  gi_composite reverts a non-finite pixel to the game's diffuse, after a 3x3 history gather. A mean
  bounce above 65504 would store +Inf and drop the bounce of about 36 full-res pixels while the
  occlusion stays: darker, with a finite scene colour.
- Guard (hit-GI variants only, key-off SPIR-V byte-identical): a qualified-hit term that is not finite
  and >= 0 takes fallback0 (the key-off ray), counter 417; a mean bounce above the half maximum is
  stored as 65504, counter 418 (energy above the encoding limit, counted, never +Inf). Both are on the
  `GI hit direct` line: `non-finite hit terms (fallback0) N, bounce over the half-float max (stored 65504) M px`.
  Checked against ac57418: the 48 key-off gi_trace_rt / gi_trace_rt_sharc requests are byte-identical,
  all 448 hit-GI requests compile and pass spirv-val.
- Rerun of m4p3 decides: N = M = 0 in diag-on means the darkening has another cause (next suspects:
  the hit-GI dispatch's alpha set from `rt_hit_material_use` instead of `b.alpha`, i.e. occlusion; and
  the scene drift between shots, the em-off pair alone spreads 0.80%).

## Sun at qualified hidden hits (`gi_hit_sun=0`, M4.3, 2026-11-01; offline, READY_FOR_LIVE)

Key `gi_hit_sun=0` (live; GUI Experimental "Sunlit hidden-surface bounce", `exp_gi_hit_sun`,
needs `exp_gi_hit_direct`). Variant `gi_trace_rt_hit_sun.hlsl` (+ SHaRC twin), chosen by the
core only with the key on: key off runs the unchanged `gi_hit_direct` SPIR-V.
- What: at a hit where `gi_hit_direct` has a supported point/spot term, the sun is added with
  rf_hit_shade's code (`rt/native_hit_sun.hlsli`): colour x clouds x b13[0], the same BSDF,
  one RT shadow ray sampling the sun disc as rt_shadow.hlsl (sun_radius), the AO-on-direct
  factor rule of rf_hit_shade. bounce += (direct + sun) x w, occ += w as before.
- Energy: still only the fallback-0 zero bounce is replaced (screen and SHaRC hits never reach
  this code), so no light already in the screen-space or cache radiance is counted twice.
  Unsupported sun (roof, unknown visibility, missing sun inputs) leaves the direct term alone.
- R0 roofs: without `rt_cascade_casters` a hit whose sun ray passes a roof volume, or any hit
  while the volumes are unknown, has no sun (counted). With the casters the sun ray sees them.
- Layout: GI push set (unchanged), material set 1, empty set 2, D-LS sun set 3 (b8 roof volumes,
  common/native_hit_gi_bindings.h `sun`), SHaRC set 4. Counters 320-328, log `GI hit sun`.
- Live gate: as M4.1 at S3 Campsite; cost = `gi: trace pass` GPU median ON minus OFF (one more
  ray per supported hit, expected +0.1 ms).

## Far-field sun bounce (`gi_far=0`, `gi_far_m=6`, 2026-11-01; offline, READY_FOR_LIVE)

Key `gi_far` (live; GUI Experimental "Far-field sun bounce" off / 4 / 6 / 8 m, needs `exp_gi_hit_sun`).
Variant: request flag `kGiFar` adds `RT_GI_FAR=1` to the four gi_hit_sun files only (common/shader_variant.h);
key off runs the unchanged SPIR-V (all 368 existing gi_trace_rt* requests byte-identical, checked). The rule is
`shaders/gi/gi_far.hlsli`, the ray length is `Push::radius` (gi_trace's march radius, unused by this pass).
- How the native ambient meets RT GI today (replacement per ray, not a residual): giRaw.a = 1 - sum(occ) / rays
  replaces the game's SSAO in the AO target (gi_ao_apply.hlsl, `lerp(ao, vis, gi_ao)`), so it scales the game's
  diffuse SH/probe ambient and, through Lagarde's SO, its specular ambient; giRaw.rgb is added to the diffuse x
  albedo (gi_composite.hlsl). A hit with weight w removes w of the ambient for its ray and adds its radiance x w.
- gi_far: rays reach R = gi_far_m. A hit with known radiance replaces the ambient with the far fade w = 1 - (t/R)^2:
  screen hits (last frame's radiance), and qualified native-material hits with a supported sun term (plus the
  point/spot term; off screen outside the froxel volume it is declared 0, local lights were 1-16% of the
  reference). A qualified hit keeps rho x w of the ambient (rho = hit diffuse albedo luminance): its own
  ambient-lit radiance, approximated as rho x the receiver's ambient, which the direct terms lack (the
  reference's multi-bounce share is 14-22%, the same order).
- Unqualified hits beyond 1 m (no material, unsupported sun or lights, aborted query): exactly the old rule,
  occlusion with the base fade w0 = 1 - (t/R0)^2, 0 beyond R0, so the native ambient stays for that ray. No
  declared fallback material: the true far-field gap is uncertain (below), so only rays whose radiance we
  know change. Misses stay misses (ambient kept).
- Roofs: GI rays do not see the cut-away roofs (rt_cascade_casters puts them in mask 0x02, sun rays only).
  A far qualified hit whose ray segment passes a padded roof volume (gi/sun_roof.hlsli boxes, sun set b8;
  every ray from inside a roofed room), or any while the volumes are unknown, takes the old rule (counted).
- Denoiser (RR): the same giRaw / temporal / composite chain; RR sees the composited diffuse. The input gets
  more variance: far hits are sparser and brighter (a sunlit wall at 4 m behind a dim receiver), at 1 spp per
  ray. Watch for stippling in the shots; the temporal pass and RR are unchanged.
- Energy: per ray, ambient visibility and bounce change by (w - w0) for screen hits and by w(1 - rho) - w0 /
  (direct + sun) x w for qualified hits. Mean brightness rises where the replaced native ambient is darker
  than the traced far radiance and falls otherwise; the `GI far` line (rt_stats) gives both, ON and the old
  rule evaluated on the same rays of the same frame, so the live mean change must match its sign and size.
- Counters 372-382 (per thread over its rays, wave sum, one atomic per 8x8 group into the host-visible rt_stats
  buffer; rt_stats windows only). A device-local buffer would need a new hit-GI binding (shared contract).

Offline (S8 pack 071930, frame_replay `--define RT_GI_FAR=1 --define GI_FAR_M=m`, the gi_hit_direct variant: no
sun set, 0 qualified hits since the pack has no VT cache, so only the screen tier is exercised):

| R | bounce luminance | giRaw.a | GPU ms, 2.73 Mpx (rt_stats 0) | at 4K Performance |
|---|---:|---:|---:|---:|
| 1 m (off) | 0.00693 | 0.916 | 0.58 | - |
| 4 m | 0.01046 | 0.886 | 1.63 | +0.80 ms |
| 6 m | 0.01177 | 0.875 | 2.15 | +1.19 ms |
| 8 m | 0.01285 | 0.867 | 2.69 | +1.60 ms |

The PT's visible-far tier at the calibrated scale is 0.0110 (14.7% x k PT total; with roofs 9.7% x k PT total,
the same 0.0110): 4 / 6 / 8 m recover 32 / 44 / 54% of it. The hidden-far tier (41.5%, with roofs 23.0%)
needs qualified materials: not measurable on this pack. In game add the hit shading at qualified far hits
(gi_hit_direct + sun measured +0.52 ms at 1 m). GPU fixture case 10 checks the per-ray rule exactly.

Roof-corrected reference (coordinator audit, 2026-11-01): the pack has rt_cascade_casters=0 and S8 is mostly
under cut-away roofs. `PT_ROOFS=1` (tools/pt_roofs.py, the asset DB's 159 CTY_Main_A roof volumes) occludes sun
rays through the padded volumes (the production rule) and ends path rays at the roof plane:

| S8 PT | no roofs | roofs (plane +3 m) | roofs (plane +0 m) | sun rule only |
|---|---:|---:|---:|---:|
| PT total (luminance) | 0.152 | 0.0111 | 0.0110 | 0.0115 |
| sun / local | 98.9 / 1.2% | 84.4 / 15.7% | 84.3 / 15.7% | 84.8 / 15.3% |
| first hit beyond 1 m, on screen (visible + hidden) | 56.2% | 32.7% | 32.2% | 31.9% |
| first hit off screen (not split by distance) | 29.5% | 59.8% | 60.2% | 60.7% |
| first rays: miss / roof | 64.2% / - | 28.2 / 37.5% | 27.4 / 39.2% | 64.2% / - |
| live domain (1 m, fade) | 11.6% | 5.7% | 5.8% | 5.6% |

**Go / no-go.** The far-field share falls from 56-86% to 32-92% (the off-screen part is not split by distance),
the sun's from 99% to 84%, and the PT total by 14x: most of the uncorrected far field was sun through roofs the
camera removed. The calibration k rises from 0.49 to 10, so most of S8's screen radiance comes from light the
PT lacks (the native ambient, emission). gi_far is kept energy-safe (native for unqualified hits and misses).
Promotion needs the corrected number above about 30% far field from a pack with casters on, real materials (VT
cache) and the native ambient (another agent adds the ambient capture), then the live batch
`tools/live_batches/gi_far` (camp + S8, OFF / ON / OFF, about 8 min).

## `gi_bounce_spec`: not re-added (review 2026-11-01)

What it did (removed dbbe0fc, 2026-10-31): `prev_bounce_at` returned last frame's full radiance
(diffuse + the specular the camera saw) as the radiance a GI ray picks up at a screen hit,
instead of its diffuse part `rgb x a`. Verdict: **not physically valid, and its valid form
already exists.**
- Wrong direction: specular radiance leaves the hit towards the camera. A GI receiver sees
  the hit from another direction, where the hit's lobe has a different value (usually far
  smaller; the sun glints are the extreme case). Reusing it is view-dependent radiance applied
  to the wrong outgoing direction, not a BRDF split. The measured symptoms were that error:
  sparse bright 1-spp bounce hits (QA S3 2.3 per mille), the S14 halo of moving highlights
  (flicker 2.84 per mille, fail; docs/QA.md).
- The valid term, the hit's specular towards the receiver with one Fresnel split
  (diffuse + specular <= 1, white furnace max 0.968, GPU fixture 6c), is what `hd_scatter`
  (FresnelBlend) evaluates at qualified hidden hits with `outgoing = -ray direction`
  (`gi_hit_direct`, `gi_hit_sun`). No double counting with D-RF: D-RF is the receiver's own
  specular; this path is receiver diffuse <- hit specular.
- Open gap (a new feature, not a re-add): screen hits still bounce only the hit's diffuse,
  so metals (no diffuse) bounce nothing from screen hits. The valid fix evaluates the hit's
  lights through `hd_scatter` at screen hits too (a light loop and shadow rays per GI hit:
  the cost of `gi_hit_direct` on every hit). Candidate for the M4 backlog, not started.

## Next
- QA and defaults for the radiance cache (above); its traced update (v2).
- ReSTIR-style reuse.
- Screen-space contact shadows for the direct lights. Real RT shadows belong to Track D.

## Derived depth tolerances (2026-10-27)
Every same-surface depth test now comes from one helper, `shaders/rt/depth_tol.hlsli`
(QUALITY R0). Two depth values of one surface, `lat` render pixels apart on screen, differ
along the view ray by at most

    depth_tol = lat · f · (tan θ + 1) + n · prec(dist)

- `f = dist · pixel angle` (0.835 mrad at 2560x1067, vertical FOV 0.838 rad): 4.17 mm at 5 m,
  25.0 mm at 30 m. `tan θ` is the plane's depth slope (θ between the GEOMETRIC normal and V);
  the `+1` bounds curvature (the sag of any circle over a chord s is ≤ s).
- `prec`: the game rasterises D24, reversed Z, infinite far, near 0.1: one rounding moves z by
  z²/0.1·2⁻²⁵ (+ the R32F's z·2⁻²⁴): 7.7 µm at 5 m, 0.27 mm at 30 m.
- `|N·V|` is moved towards grazing by its own error ε = max(fp16 storage √3·2⁻¹², depth-normal
  2·prec/f): 0.0037 at 5 m, 0.022 at 30 m; floored there (tan θ ≤ 1/ε).
- Jitter (Halton ±0.5 px per axis) adds 0.71 px per jittered depth sample.
- Measured, not derived: TLAS against raster, 0.5% of the distance (TRACK_D_ALPHA_GI.md 7.1: 99.09% of
  primary rays within the 0.5% bin; the rest is other geometry). Only where no per-pixel TLAS
  reference exists; `rtOrigin.w` is that reference (rt_origin=1).

| Test | lat (px), n | Old | New, 5 m, N·V 1 / 0.2 | New, 30 m, N·V 1 / 0.2 |
|---|---|---|---|---|
| GI on-screen hit, rtOrigin reference | 1.41, 1 | 3% (150 / 900 mm) | 6.4 / 35 mm | 43 / 231 mm |
| same, no reference (rt_origin=0) | + 0.5% | 3% | 31 / 60 mm | 193 / 381 mm |
| rt_origin match (and search window) | 0.71, 1, + 0.5% | 17 / 83 mm (window 67) | 28 / 43 mm | 172 / 266 mm (window was 401) |
| GI temporal, derived part (typ.) | 2.4-3.5, 2 | 3% of z | 11-16 / 60-89 mm | 74-108 / 394-577 mm |
| bilateral σ at 0.71 / 2 / 3.3 px | as is, 2 | 1% (50 / 300 mm) | 3.2 / 9.1 / 15 mm; 18 / 50 / 83 mm | 22 / 61 / 101 mm; 116 / 327 / 543 mm |

- The temporal and bilateral tests compare camera distances (rotation invariant), not view z.
- The temporal test keeps the old 3% as a separate camera-translation term: not derivable in
  that pass (no previous camera position bound). Open: pass |Δcam| per frame.
- The bilateral's centre ×2 is the denoise kernel's shape, unchanged.
- `gi_rt_depth_tol` / `rf_depth_tol` are gone; `gi_rt_depth_k` / `rf_depth_k` (1) multiply the
  derived tolerance, for A/B only (new names so an old 0.03 is not read as a multiplier).
- An aborted GI ray query (candidate cap, `RtHit.aborted`) is a hidden hit under
  `gi_rt_fallback`, weighted by the fade's mean over its unknown distance, never a miss.
- `qa_zero` bit 2 (gi_ao_apply.hlsl) writes the AO 0: the diffuse AND the specular ambient go
  (SO = 0 at AO 0); direct light keeps 1 − m0.y = half (traced local lights get it back with
  ao_direct=1); the GI bounce and the reflections stay.
