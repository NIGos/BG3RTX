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

S6 darkening (m4p3 112657: step-gi -1.05%, +sun -1.82%, +inst -3.10%; 2026-11-01, branch
worktree-agent-a128d3e3a3182b5e8, offline): the forge's animated lava, not the bundle (live confirmation pending).
- Shader, offline: the S8 replay pack run with the base pass (`gi/gi_trace_rt.hlsl`) and the hit variant on the
  same rays (`py -3 -m tools.gi_hit_invariant`): giRaw.a bit-equal on 100% of 2.73 M pixels, mean luminance +0.0755%,
  6 pixels lower beyond the half rounding (rays the two compiled shaders classify differently at the screen hit's
  depth test, both directions; lost 3.0e-8 vs added 6.8e-6 per pixel). So the occlusion is the key-off one: the
  material set (`alpha_clone_hit_set`) copies bindings 0-3, 6-11 and 19 of the scene's alpha set, the traversal is
  the same. Excluded: a different alpha set (more occluded rays) and an ambient term removed at qualified hits.
  Also excluded, by reading: lost lava glow. fallback0 adds no bounce and screen hits read prevRadiance without the
  effects target, so a shaded hit replaces 0, never a glow.
- The steps cannot carry their own darkening: in diag-on the sun added 0.75 against 11208 for the point/spot
  term (indoors), and the instanced rows are 0 at S6 (`wind 0 rows, 2S 0 rows`). The +inst shot also includes
  rf_hit_shade (step-rf had no trace), whose tier (a) check is brighter (L_b / L_a 1.09).
- Where it sits (the traces' PNG previews decoded to linear, `tools/preview_energy_split.py`; decoded frame means
  within 0.07% of the float means): lava pixels hold 54.5% of the frame's luminance. Of inst's -3.10%, -2.56% is
  lava; the rest is -1.07% relative, against -0.87% / +0.87% for the two OFF shots on the same pixels (the frame
  spread 0.07% was a cancellation: lava +0.32% against the rest -0.40%). The lava change follows the game's glow
  buffer (pass 48eedb24, a game pass before the lighting that reads no output of ours we know of): OFF +8% / -8%, gi -5%, sun -16%, inst -25%, ON shots
  -6 to -24%. The m4p3 step shots also came 1-2 frames after a 4 s DXC compile of each new variant (1-2 fps,
  a 5 s GPU stall before step-inst): the warmup compiled only the all-on variant.
- So at S6 two OFF shots cannot set the band: the scene moves by about 1% per shot. Live check prepared,
  not run: `tools/live_batches/m4_promotion/s6_diag` (one S6 replay pack for the offline invariant, 5 OFF / 4 ON
  interleaved shots with every variant precompiled, gi_debug=3 and 2 pairs; README has the predictions).

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

## Deferred hidden-hit sun visibility (rejected and removed, 2026-11-05)

The `gi_sun_wave` prototype deferred eligible hidden-hit sun rays into a dense dispatch,
then replayed individual additions in original ray order. It was 100% bit-equal in outputs
and raw counters on S8/camp/inn, statistics on and off, but all paired timings were slower.
Forty ABBA/BAAB samples per pack gave ON-OFF median [95% median interval], in ms:
S8 +0.735776 [+0.540272, +0.849120], camp +3.114312 [+2.451616, +3.897856],
inn +0.737392 [+0.638336, +0.863264]. The 1,158 MB scratch did not earn a speed gain.
Code, key, settings, specialization wiring, two requests and feature tests are removed.
SpecId 1034 is retired, never reuse. Requests return to 125. Paired replay remains.
The cleaned ordinary shaders are byte-identical to main for all 215 unique requests.
Cleanup full build and all 191 CTests pass (158.05 s).
See [sunwave](progress/sunwave.md) for retained receipts and current checks.

## Far GI cost split, 2026-11-05

Offline RTX 5090, main `5b076d9`, S8/camp/inn, sun trust, radius 6 m, two rays, statistics off.
Each entry is its own 40-sample ABBA/BAAB median [95% median interval], ms.
N=near; Q=6 m query with extra hits discarded and near math retained; L=far/screens without extra hidden shading;
Fc=full far with exact frozen GI roof answers; F=original far. These conditional program costs are not additive.
N/Q outputs/near shading counters match; roof controls match all output/counter bits, with zero cache/mismatch guards.

| Pack | (a) Traversal Q-N | (b) Extra hidden shading Fc-L | (c) Matched GI roof loop | (d) Other policy/context L-Q |
|---|---|---|---|---|
| S8 | 0.325 [0.324, 0.326] | 1.562 [1.227, 2.396] | 0.465 [0.351, 0.772] | 0.562 [0.561, 0.563] |
| Camp | 3.787 [3.641, 4.060] | 2.037 [1.500, 3.377] | 0.332 [-0.074, 1.362] | 3.217 [2.761, 3.822] |
| Inn | 2.065 [1.973, 2.329] | 0.926 [0.512, 1.154] | 0.374 [-0.007, 0.650] | 1.711 [1.372, 2.074] |

(c) retains both bodies in one SPV, selects via runtime push comparison/SpecId 1900, and charges both for the same lookup;
it measures the original loop plus verification comparison. Only S8 excludes zero. Simple real-minus-cached substitution
was confounded (0.204 / -0.668 / -0.387 ms), so is not called a physical loop cost or combined with (c).
Fresh F-N totals: 2.762 [2.555, 3.102] / 9.132 [8.331, 9.627] / 4.702 [4.350, 4.928] ms; historical
2.430 / 5.460 / 2.448 ms are a different context. The original four-edge telescope closes within its intervals.
(d) includes fade/base-fade, screen/near outside-froxel eligibility and compilation context. Statistics atomics do zero work;
original near/far driver registers rise 168 -> 255. Anomalous memory figures support no spill/occupancy inference.
Native sun roof work stays in (b); cascade casters bypass that loop in camp/inn. Screen/hidden memo overlap is zero everywhere.
Inn's added hidden shading changes no stored output: all qualified far hidden hits are roof-vetoed.

Two exact candidates (implemented and measured below); the original planning estimates were:
- Early inside-roof rejection after emission, before hidden shading, only t > RB: the ORIGINAL ray origin inside the first
  padded roof proves the segment veto and zero base-fade fallback. Keep diagnostics and conservative unknown/non-finite handling.
  Covered extra-qualified cohorts: 75,984/246,920 S8; 21,144/25,699 inn. Proportional-work targets less guard cost:
  0.2-0.5 ms S8 / 0.4-0.7 ms inn; camp has no covered cohort and could lose.
- Exact-bit (0,0,0,1) quaternion path: omit two rotations, retaining subtraction, slabs/order/boundaries and finite/subnormal
  fallbacks. It covers 76.6% / 100% / 100% of GI roof volume visits. A 20-40% roof-work target suggests
  0.1-0.2 ms S8 / 0-0.2 ms camp/inn, whose loop costs remain unresolved.

Implementation and coordinator correction (roofrej, 2026-11-05): restore only early rejection under new SpecId 1038.
`gi_far_roof_early` was initially a developer raw key, default 0 and release-forced OFF; qualification below promotes it ON. IDs 1035 / 1036 remain retired;
identity stays removed (its camp production loss was +0.080 [0.048, 0.117] ms). Request delta 0, ABI unchanged.
The coordinator overturned early rejection's diagnostic-mode rejection: production uses rt_stats=0, whose earlier
S8/camp/inn intervals passed (-0.748 [-1.034,-0.400] / -0.002 [-0.483,+0.393] / -1.023 [-2.393,-0.394] ms).

The host now forces specialization 1038=0 whenever rt_stats != 0. Key ON/OFF then select the identical module,
specialization payload and cached pipeline slot: diagnostic shading, counters and executable allocation stay original.
The shader also retains its pc.rt_stats==0 guard. The original-origin/first-padded-roof/t > RB proof remains after
emission, with conservative unknown/non-finite/subnormal fallbacks. All three modules (S8, cascade, Ultra emission)
are byte-identical to the prior early-only candidate after renaming the SpecId decoration.

Fresh ID 1038 proof: S8/camp/inn, sun trust, 6 m, two rays, all outputs and raw counter bins 100% bit-equal.
At rt_stats=0 compare 0/1 for 40 ABBA/BAAB pairs; at rt_stats=1 compare both main vs candidate 0 and host key
ON/OFF 0/0 for two pairs each. Fresh production ON-OFF median [95% median interval], ms:

| Pack | rt_stats=0, 40 pairs |
|---|---|
| S8 | -1.734 [-2.734, -1.398] |
| Camp | +0.495 [+0.103, +1.696] |
| Inn | -1.361 [-2.429, -0.798] |

The 40-pair camp loss conflicted with the earlier inconclusive result; the restored candidate remained OFF for coordinator review.
The subsequent stability check below did not reproduce a camp loss. Camp identical OFF/OFF control, 40 pairs:
+0.128 [-0.821, +0.323] ms, all output/counter bits equal. No candidate timing was discarded or rerun to select a pass.
Subsequent camp stability check (no production changes): three back-to-back 80-pair A/B runs, each preceded by
its own 80-pair OFF/OFF control, GPU idle before each phase and no bg3/other game. ON-OFF median [95% CI], ms:

- Run 1: -0.005904 [-0.143744, +0.059024]; control +0.008064 [-0.132944, +0.245616].
- Run 2: -0.110912 [-0.211504, +0.079696]; control +0.022808 [-0.174240, +0.104528].
- Run 3: +0.100592 [-0.012960, +0.200400]; control +0.000448 [-0.076752, +0.093728].

Pooled all 240 raw A/B pairs: +0.005064 [-0.060240, +0.059024] ms, same exact-binomial median interval.
No reproducible camp loss; every timing output/counter bit matched. Separate scratch-only output view counted
0 early rejections from 492,480 guard evaluations across 5,463,040 rays. Hence no hidden shading is saved on camp.
A cost could come from per-hit volume loads, rotations, finite/containment/slab tests or compiler live-state changes;
these timings do not establish a nonzero net cost. Cheapest candidate if needed: reject non-finite/outside local
origins before direction rotation/slabs. Existing SunTrustFrame.volumes permits a host zero-roof bypass, but camp
has 15 volumes: it cannot skip this pack. A dispatch-wide bypass needs conservative disjoint receiver-origin/first-roof
bounds; camera-outside alone is insufficient. No code changed. Worktree receipts: `.scratch/roofrej-camp-repeat/summary.json`.

Registers unchanged: compact hit state 0, near 168 / far 255; production compact state 1, near 168 / far 243.
Both OFF/ON match main. Key-off equivalence vs main `9b38f38c`: all 215 requests / 2,048 assignments pass;
negative control passes. Full `scripts/build.ps1 -Test`: all 192 CTests pass (169.53 s).

`tools/live_batches/opt_r6/roof` is prepared READY_FOR_LIVE, not staged or launched: exact compact_hit observation
profile/schedule with qualified compact state 1 fixed, inn + S8 Ultra, dev=1, four static OFF/ON/OFF/ON 600-frame
windows and fresh QUIET. GI trace decides; JSON and stdout show preceding-OFF pair deltas for each ON window.
The readiness TABLE maps gi_far_roof_early to [GI_TRACE]. Additive extend_readiness adds six queries only;
all earlier receipts/models remain unchanged. Frozen and non-frozen lints pass 2/2. Build-free roof_jobs is in the
single CMake foreach; no merge_scope build-only exclusion applies. READY_FOR_LIVE is job preparation status,
not an offline performance qualification; coordinator review and explicit user steering precede any live batch.

Sun replay packs lack emission descriptors; emission ordering is unchanged and the production emission request
supplies registers. Trust inputs: CTY_Main_A / WLD_Main_A / SCL_Main_A, 159 / 15 / 37 volumes.
Fresh receipts: main checkout `.scratch/roofrej1038/summary.json` and adjacent commands, modules, counters, CSVs and logs.
Historical ID 1035/1036 receipts: `.scratch/roofrej/summary.json`. Historical attribution's `.scratch/far-cost/` was absent.
The shared proof normalizer repeats its optimizer schedule to clean constant branches exposed by specialization.
The offline preparation above made no game, queue or git writes. Builds BelowNormal; GPU only without bg3.exe.

Qualification (roof-on, 2026-11-05): keep SpecId 1038 as an always-on exact optimisation, like
`rt_compact_hit_state`. Defaults, every preset and release policy force `gi_far_roof_early=1`;
`dev=1` preserves a raw 0 for A/B. No player knob. Host statistics mode still forces the original path.
S8 Ultra `opt-roof-s8.1.211159` VALID: GI trace OFF/ON/OFF/ON 7.655/6.275/7.937/6.226 ms
(-1.38/-1.71 ms); GI total 12.349/10.514/12.386/10.382 ms. RR costs about +0.2 ms in ON windows.
The inn run is INVALID (last window short); complete windows give about 12.2 ms ON vs 12.5/13.2 ms OFF.
Offline paired trace deltas: S8 -1.73 ms, inn -1.36 ms; camp neutral over 240 pairs,
+0.005 [-0.06,+0.06] ms. Outputs are exact (bit-equal). Request delta 0; ABI unchanged.
In-game check: S8 Ultra, inspect roofed-room far bounce; developer A/B should leave the image unchanged
while reducing GI trace time. These are supplied live results; this qualification edit is offline only.

## Far-field sun bounce (`gi_far=0`, `gi_far_m=6`, 2026-11-01; live S8 energy gate failed, kept off)

2026-11-04 outcomes. One-ray Ultra GI (`ultra-gi-1ray`, gi1 live 17:48, all 16 window gates OK): GI trace
6.74/4.71/9.22/4.48 ms (OFF-a/ON-a/OFF-b/ON-b), saving 3.27/3.50 ms. Float energy ON -7.8%/+3.9%, inside the
OFF repeat spread 11.6%. Static flicker: ON-a passes, ON-b fails against both OFF repeats (PQ std excess
0.0032/0.0022 > 0.00098). Pan registration unavailable. Rejected for Ultra: quality before frame rate; Ultra keeps
two rays. Roof-leak fix (`2aa9d0f3`) kept: rel1 17:52 VALID, GI 11.46 ms vs 11.97 ms before, no measurable cost.

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

### Go / no-go on the full pack (2026-11-01 evening, offline): **keep off**

Pack `replay-20261101-142819-1` (rc1, S8, `rt_cascade_casters=1`, VT cache 2054 MB, lighting inputs 19 of 19).
The caster instance is in the pack's TLAS array but has no geometry (InstInfo tris 0, left out with mask 0), so
the replay still needs the asset-DB roof volumes: `PT_ROOFS=1` for the PT, and for the pass the new
`frame_replay --sun-trust` (the sun set b8 image from `tools/pt_roofs.py --trust`, 159 padded volumes).

- Reference (PT_ROOFS, PT_SUN_LUT, PT_ENV, real materials, 32 iterations): far field = 53.6% of the traced bounce
  on screen beyond 1 m (visible 15.2, hidden 38.3) + up to 35.8% off screen (not split); live domain 8.4%; sun /
  local 70 / 30%; multi-bounce 24.9%; first rays: roof 39.6%, miss 20.0%. k = 15.2: the PT's sun + local light is
  1/15 of the game's radiance at visible hits (S8 is lit by the native ambient). Native vs PT far + env:
  E x AO / (k x PT far + env) **1.44** (tiles p10 0.90, median 1.75, p90 3.01); E / ref 1.76; k = 1: 1.91.
  The preliminary 0.70 (no roofs) flips: sun through the cut roofs made the old reference too bright.
- Emulation: the gi_hit_sun pass with `RT_GI_FAR=1 GI_FAR_M=6` on the pack reproduces the live `GI far` line
  (ambient visibility 0.8326 vs live 0.8343, old rule 0.8790 vs 0.8803; bounce 0.0146 vs 0.0147, old 0.0090 vs
  0.0092; screen hits beyond 1 m 5.1% of rays vs 5.0%). Qualified sun-lit far hits 0.10% (live 0.28%: the volume
  rule is stricter than the casters).
- Image change it predicts (diffuse = dcol x (E x a + giRaw.rgb), gi_ao = 1; the native model explains the pack's
  diffuse radiance, median ratio 1.13): 4 / 6 / 8 m **-1.4 / -2.0 / -2.4%** of the screen radiance (6 m: ambient
  -2.83%, bounce +0.88%). Toward the reference, no overshoot: the game stays above it on 89% of tiles, per-tile
  |I - ref| / ref 1.40 -> 1.31 (6 m) -> 1.30 (8 m), mean I / ref 1.555 -> 1.507 -> 1.497.
- **Live S8 went the other way: +2.87%** (raw; the eye adaptation, see the next section: -0.32% at a fixed exposure)
  (scene colour 5c3fdabc, OFF spread 0.23%; diffuse buffer d1669194
  +2.93%). The bounce alone gives at most +0.88%, so the live change is not explained by the `GI far` line: the
  batch's energy gate fails. The ray data agree, so the difference is downstream of giRaw (temporal, AO fold,
  composite, the D-LS `ao_direct` give-back that reads the folded AO, RR). Camp (+3.97%) has no pack.
- Verdict: keep `gi_far` off. No rule change indicated: the rule moves S8 the right way and longer rays help
  monotonically, and `gi_far_rho` touches 0.1-0.3% of rays. Next: one live window ON / OFF with `ls_stats=1` and the
  GI images in the trace (giRaw, the folded AO, gDiffuse before and after the composite) to find the stage that
  turns -2% into +2.9%; then rerun gf2. Cost if ever on: GI trace S8 0.97 -> 3.00 ms, camp 2.63 -> 5.70 ms.

### Live energy: the eye adaptation, not a GI stage (2026-11-01, offline from the gf1/gf2 logs and traces)

- Derivation, per pixel (gi_ao = 1, ao_direct = 1, m0.y = 0.5, RR mode: gi_temporal passes giRaw through and
  gather_history is a normalised depth-weighted mean, both linear and the same for a and rgb):
  D = k (E dcol A + f(A) D_untraced + D_traced) + dcol B, with A the folded visibility, f(A) = 1 - 0.5 (1 - A), k the
  pre-exposure s1b13[0]. The give-back makes traced direct light A-independent: the game writes td f(A), D-LS adds
  td (1 - f(A)) (its cap can only lower that). So dD = k (E dcol + 0.5 D_untraced) dA + dcol dB + D dk / k: with
  dA < 0, every GI stage can only darken except the bounce (+0.88% offline); the sun's give-back adds only where its
  recompute exceeds the game's sun (lit on 7.5% of S8 px). The SSS pass and the compose are linear; RR comes after
  the measured buffers. No stage of ours can turn -2% into +2.9%.
- The traces say it is dk: the change is a uniform factor. S8 diffuse +2.91 / +2.93 / +2.94% in R / G / B; per tile
  (16 x 9, PNG decoded) ON / OFF median 1.030, p10 1.021, p90 1.037, the same width as OFF-b / OFF-a (0.993-1.010).
  The `[rrpost] exposure probe` lines at the trace frames: s1b13[0] OFF-a 3.05704e-4, ON 3.15530e-4, OFF-b 3.05773e-4
  (EV 11.413 -> 11.367 -> 11.412): ON / OFF 1.0320. Camp 9.4642e-5 / 9.8496e-5 / 9.4566e-5: 1.0411. The ON shots
  also show the adaptation still moving (re-exposure 0.996 / 0.995, every OFF shot 1.000). The gf jobs had no
  `qa_exposure_hold`.
- At a fixed exposure: **S8 scene colour -0.32%, diffuse -0.26% (OFF spread 0.21%); camp -0.14% (0.11%).** The sign
  agrees with the offline -2.0%; the size does not (S8 -0.3 vs -2.0%). That gap is open: the next gf2 run holds the
  exposure and measures the stages (tools/live_batches/gi_far README "Stages": folded visibility, bounce, give-back).
- Fix of the test: `tools/m4_promotion_gate.py` (the gate table of every lean batch) divides each scene colour by
  its trace frame's s1b13[0] and prints the raw change and the exposure ratio beside it (CTest
  m4_promotion_gate_test); the gi_far plans hold the exposure. The m4_promotion runs moved little (camp shot-on
  exposure 0.9963: -0.12% raw, +0.25% normalised).

### The traced frames held the key-off GI (2026-11-02, gf2 015729 + offline replay)
- Held run: S8 scene colour ON -0.10% (band 1.38%), view vis ON / OFF 0.9998 (expected 0.948), while the same run's
  `GI far` line (0.840 vs 0.887) and the ls give-back (x2.4, 120-frame stats) moved. Replay of the hit_sun RT_GI_FAR
  pass on pack 142819-1 with the live push (`--push radius=6`): giRaw.a 0.8320 vs 0.8790 off, with rt_stats 0 and 1:
  the pass is correct.
- Cause: in a trace frame the G-buffer pass was not taken as the hit-material capture (layer/geocapture.cpp, the
  `!trace_recording()` condition), so the frame had no deferred packet, `rt_hit_material_use` failed and the hit GI
  fell back to the base pass (1 m, no hit lighting) in exactly the frames the gates measure. Every trace-based energy
  of a hit-material consumer (gi_hit_direct, gi_hit_sun, gi_far, rf_hit_shade, hit emission) showed the key-off GI.
- Fix: the trace frame keeps the hit-material capture unless the trace takes the G-buffer pass itself (geo_capture=1).
  Rerun gf2 only; predicted: view vis 0.947, scene colour -2.0% (predicted.json).
- Rerun gf2 031330: -2.01% (band 1.32%), view vis 0.9471: as predicted; camp 033251 -2.42%. 6 m joined Ultra.
- **Roof-cut leak of far screen hits (roof-leak, 2026-11-04).** Confirmed: the screen path read `prevRadiance`
  without the hidden path's segment test. A matched screen hit beyond the base radius now uses the same
  `gi_far_roof_clear(o, dir, t)`: intersecting padded roof volumes or unknown roof data reject the entire ray.
  Rejection retains native ambient (base fade is zero) and precedes emission, SHaRC and fallback shading.
  No new key, binding or request; the guard is inside existing `gi_far` specialization 1007. Near hits and far-off
  paths are unchanged. Offline fixtures cover roofed origins, canopy crossings, finite segments, rotated volumes,
  camera-relative coordinates and unknown data; verification receipt is in `docs/progress/roof-leak.md`.
  No game launch or GPU cost measurement. Future in-game confirmation: interior under a cut-away roof with
  on-screen exterior ground (S8 roofed areas); check reduced exterior bounce without changing open-air bounce.
- Cheaper option measured (replay, 2026-11-02): gi_rr_rays 1 keeps the mean (S8 giRaw.a 0.8321 vs 0.8320) at 1.73
  vs 4.31 ms (camp 3.90 vs 7.36 ms); its noise through RR is the live job sharc_far sh1 (second section).

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

### gpu-opt1: exact state and immutable metadata candidate (2026-11-03)

2026-11-04: Three GPU candidates REJECTED by the image gate: compact hit state / alpha metadata cache / empty water tiles saved 0.53/0.11/0.19 ms; no release promotion.
code removed 2026-11-05, evidence above

`rt_compact_hit_state` is off by default (specialization constant 1024). Closest-hit
transform retrieval moves after traversal, groom selection and barycentric extraction.
Only proven non-pool instances omit the query transform: every transform consumer
uses object-to-world only for pool geometry. Out-of-range instance metadata retains
the original query transform and existing consumer guards. Pool matrices retain all
float32 operations, including scene rebase. This is a limited lifetime reduction;
removing the pool matrix across projection needs a separate transform source and is
not implemented. GI reuses the same hit normal for direct, sun and AO, and lazily
caches read-only light descriptor ranges and b13[0] across this invocation's rays.
No lighting, ray, material, alpha, precision or sampling policy changes.
Offline equivalence and camp/S8 OFF/ON/OFF cost/identity results are recorded in
`docs/progress/gpu-opt1.md`; no GPU saving or qualification is claimed before them.

GPU fixtures: lit/dark GI, 4,096 rigidly transformed pool hits and real solid/clear alpha candidates pass exact giRaw, depth and statistics bytes for each candidate and combined. This is fixture coverage, not full-scene or cost qualification.

Key-off gate: all 173 requests/194 entries match HEAD after specialization; SPIR-V validation passes. GPU ms remain unmeasured. Camp/S8 live drafts and the static identity analyzer are in `tools/live_batches/gpu_opt1`.

## Lower-preset spatial budget (gpu-opt2, 2026-11-03)

2026-11-04: Medium Half is not shipped: about 0.25 ms saving without quality acceptance. Full trace resolution remains selected everywhere; this supersedes the candidate defaults below.
code removed 2026-11-05, evidence above
`rt_trace_scale` is 0.5 in Low/Medium, 1.0 in High/Ultra. High is unchanged because
PERF_AUDIT_GPU supplies an experiment, not quality evidence for promoting half resolution.
Low preserves its 2-ray screen-space path; RR half-grid GI uses 1 spp, current-sample temporal
weight, and existing depth-aware `gather_history` before full-grid AO/composite and dense RR guides.
Reflection half-grid traces write a dedicated RGBA16F delta/hit-distance target through
the unused GI raw slot (set 0 binding 8), preserving D-RF's 32-descriptor push layout.
Core review follow-up (core-review-fixes, 2026-11-03): asynchronous replacements
reuse an installed reflection pipeline only when its resolution mode matches the
requested bindings/dispatch/resolve. Otherwise the pass waits for the new pipeline.
A full-grid depth/normal/material-guided resolve applies deltas to native destination lighting,
with hit distance from the strongest guide sample (never averaged with the no-hit sentinel).
Diagnostic reflection debug/validation retains full resolution. Non-RR uses the spatial resolve;
Low keeps reflections off and preserves its screen-space GI budget.
The alternate output uses the existing `RT_TRACE_HALF` variant; no on/off define product
for unchanged GI. Manifest 145 -> 163 requests (+18); full-grid modules stay byte-identical.
Far/bounce/emission remain off below Ultra; existing levels are unchanged and tested.
Offline estimator check: `python tools/gpu_trace_scale_check.py`; real-scene and reflection
replay coverage remain missing. Synthetic estimator agreement alone is not a quality pass.
Prepared batch: `tools/live_batches/gpu_opt2` (camp High, S8 Medium, one launch each).
Each has 1.0/0.5/1.0 cost windows and static energy repeats; no live run is authorized here.
Expected saving 0.4-1.3 ms is the audit's hypothesis; actual pass cost is UNMEASURED.
Synthetic corrected 32-frame GI trace check: mean -3.94% within +/-10.16% (4 paired SE),
matched trace pixels exact; the wide band does not qualify composed or reflection energy.

## Reprojection guards release outcome (2026-11-05)

Both motion-sample and previous-radiance guards were rejected for release.
Key-off identity passed 2,987/2,987, but the approximately 11 GiB transient/energy
gate remained unqualified; PNG screens did not replace that evidence.
code removed 2026-11-05, evidence above

## Optimisation round 1 timing and proof limits (opt-gi, 2026-11-05)

The enclosing GI GPU timer includes pre and post injection regions: origin,
trace, temporal, AO fold, sun, local shadows, reflections, composite, water and
optional diagnostics. Hit direct/sun/emission and far bounce are inside the trace
dispatch, not separate passes. Shared RR is outside these regions. Independent
summary statistics cannot assign the 4.3-7.8 ms total-minus-trace difference.

With `detailed_logging=1`, additional asynchronous GPU query pairs report origin,
temporal, AO fold, sun trace, sun resolve, contact and composite costs. They average
successfully dispatched frames, exclude surrounding transitions/barriers and reset
with `rt_stats_reset`. No live costs measured on this branch; image path and shader
requests unchanged. Timing policy fixtures pass; see `docs/progress/opt-gi.md`.

Roof early rejection is dropped for this round: existing production replay packs
lack hit-emission set 4 and the temporal/composite output chain, so they cannot prove
complete Ultra output equality. The current-sample temporal shortcut is also dropped:
there is no production temporal format/motion/history/downstream equality fixture.
Neither candidate has a retained key, qualified saving or prepared live A/B job.


## Round 1 roof rejection outcome (2026-11-05, opt-apply1, rejected)

The later `gi_roof_reject` trial skipped hidden far-hit shading discarded by the
roof rule, retaining emission and the statistics path. Inn A/B GI enclosing total:
OFF 21.75 / 21.58 ms, ON 21.87 / 21.95 ms. No saving; rejected.
Evidence: `captures/queue/logs/opt-gi-roof-inn.1.021432.log`.
The key, specialization branch, prewarm registration, helper, candidate-only tests
and `tools/live_batches/opt_r1/gi_roof` were removed; ID 1028 stays reserved.
The existing far-GI roof-leak fix remains enabled as before.
The `tools/live_batches/opt_r2/gi_attr` attribution job was abandoned and removed:
its readiness cannot observe cached pipelines. No attribution result is claimed.
Release-path equivalence is checked against main with both retired IDs at their
baseline release value 0; retained specialization domains remain exhaustive.


## Compact hit state qualification (gi-livestate, 2026-11-05)

`rt_compact_hit_state` (always on, default 1, spec 1029; no UI knob) keeps the already
computed geometric normal across direct-light queries, allowing the full hit state
to expire earlier. Repeated range-guard counters are preserved. Ray sets, samples,
float precision and accumulation order are unchanged; shader request delta is 0.
RTX 5090 production gi_trace_rt_hit_sun_em with native, hit-instancing, cascade and
far specialization: registers 255 -> 243, spill 816 -> 44 bytes, with wind
RGB both off and on. The replayable direct-only request stays at 168 registers.
S8/camp/inn ON vs baseline: 100% bit-equal outputs and raw counters, 24 dispatches
per run. Offline replay timings overlap OFF-repeat noise.
Key-OFF release equivalence: all 214 unique requests pass, 2,120 assignments.
Promotion build and all 184 CTests pass (189.60 s), including settings and the replay fixture.
Projection-matrix reload was rejected for changing 50 S8 output components.

Live qualification: inn Ultra, two VALID runs `opt-compact-hit-inn.1.105908`
and `opt-compact-hit-inn.1.110402`. GI trace OFF/ON/OFF/ON was
12.721/12.555/13.228/12.634 and 12.683/12.563/13.115/12.843 ms.
Every ON was below its preceding OFF (-0.17, -0.59, -0.12, -0.27 ms;
mean about -0.29 ms). GI total and other passes stayed within OFF spread.
The camp run was confounded by scene drift (all passes moved about 7 ms
between pairs) and is not used. Qualified as exact (100% bit-equal offline)
with no regression. The inn GI trace gain is about 0.3 ms, not separated
from the OFF repeat spread (0.43-0.51 ms). Occupancy stays at 8 warps;
the gain is from spill reduction. Every preset and release policy force 1;
`dev=1` retains raw `rt_compact_hit_state=0` for in-run A/B at the inn.
Default-OFF shader requests retain release equivalence; the default spec
value is now 1. Shader request delta remains 0.
See [branch evidence](progress/gi-livestate.md) for timings/spreads and
[offline commands](../tools/frame_replay.md) for statistics and exact comparisons.
