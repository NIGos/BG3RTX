# Release control contract (2026-11-04)

Release means effective `dev=0`; `dev=1` exposes developer controls. Metadata is in
`common/settings.h`; schema retains hidden metadata with `developer: true`.
Bridge must use `knob_visible` / `choice_visible` for rendering AND set requests.
Presets: Low / Medium / High / Ultra, plus Custom indicator; values below are L/M/H/U.
Choice indices remain schema indices. Box On is index 1 (`all`, ls_box=2); legacy index 2 is hidden.

| Page | Label (`knob id`) | Raw keys | Choices / range | Presets | Apply |
|---|---|---|---|---|---|
| Lighting | Ray-tracing detail (`rt_geometry`) | `rt_build`, `rt_alpha` | Off / Solid surfaces / Include foliage | Off / Solid surfaces / Include foliage / Include foliage | restart |
| Lighting | DLSS Ray Reconstruction (`rr`) | `rr` | Off / On | Off / On / On / On | hitch |
| Lighting | Sun shadows (`sun_shadows`) | `rt_shadows`, `contact_shadows`, `rt_cascade_casters` | Visible surfaces / Ray-traced / Include hidden roofs | Visible surfaces / Ray-traced / Ray-traced / Include hidden roofs | live |
| Lighting | Indirect lighting (`gi`) | `gi`, `gi_rt`, `gi_rays`, `gi_steps`, `gi_rr_rays` | Off / Visible surfaces / Ray-traced / Ray-traced high | Visible surfaces / Ray-traced / Ray-traced / Ray-traced high | live |
| Lighting | Off-screen bounce light (`exp_hidden_surface`) | `rt_hit_material`, `rt_hit_material_instanced`, `rt_hit_material_wind_rgb`, `gi_hit_direct`, `gi_hit_sun`, `rf_hit_shade`, `rf_hit_gi`, `rt_hit_emission`, `rt_emissive`, `gi_far`, `gi_far_m` | Off / Near / Far (6 m) | Off / Off / Off / Far (6 m) | restart |
| Lighting | Local light shadows (`local_shadows`) | `rt_local_shadows`, `ls_samples`, `ls_shadowed` | Off / Fast / Full | Off / Fast / Full / Full | live |
| Lighting | Interior light shadows (`box_lights`) | `ls_box`, `ls_box_honour_shadow` | Off / On | Off / Off / Off / On | live |
| Lighting | Hair shadows (`exp_native_coverage`) | `rt_native_hair`, `rt_native_scalp`, `rt_native_tileset` | Off / On | Off / Off / Off / On | restart |
| Lighting | Mod lighting strength (`rt_intensity`) | `gi_strength`, `ls_strength`, `rf_strength` | 50-100%, step 5% | 100 / 100 / 100 / 100 | live |
| Surfaces | Reflections (`reflections`) | `rt_reflections`, `rf_max_rough` | Off / Glossy / Standard / Rough | Off / Glossy / Standard / Rough | live |
| Surfaces | Water lighting (`exp_water`) | `rt_water_geometry`, `rt_water_reflections`, `rt_water_compose`, `rt_water_shadows`, `rt_water_ls_shadows`, `rt_water_game_shadow`, `rt_water_refraction` | Off / Reflections / Reflections and shadows / Include refraction | Off / Off / Off / Include refraction | restart |
| Surfaces | Depth of field and bloom smoothing (`rr_post_temporal`) | `rr_post_temporal` | Off / On | Off / Off / Off / On | live |
| Performance | Frame Generation (`frame_generation`) | `fg` | Off / 2x / 3x / 4x | 2x / 2x / 2x / 2x | restart |
| Performance | Rendered frame limit (`fps_cap`) | `fps_cap` | Off / 30-240 fps, step 1 | 0 / 0 / 0 / 58 | live |
| Performance | NVIDIA Reflex (`reflex`) | `reflex` | Off / On / On + Boost | On / On / On / On | live |
| Performance | Faster bounce light tracing (`exp_gi_rt_pipeline`) | `gi_rt_pipeline` | Off / On | On / On / On / On | live (first enable may pause while the pipeline builds) |
| Performance | Bounce trace reordering (`exp_gi_rt_pipeline_reorder`) | `gi_rt_pipeline_reorder` | Off / By hit kind / By hit kind and object | By hit kind (all presets) | hitch |

Apply exceptions: FG Off/on restarts; 2x/3x/4x live. Water and stock-hair Off live;
water/stock-hair enable restarts. Bounce capture first enable/disable restarts.
RT strength affects indirect light, local shadows and reflections. Frame limit counts rendered frames.
Capability/dependency previews, startup prerequisites and existing hardware/local-mask fallback remain.

Advanced has no release controls; omit its navigation and the developer Experimental disclosure.
Release "Show experimental options" switch (off by default, saved as `experimental_options=1`) shows the
`settings::kOptInKnobs` on their own Experimental page; Off resets them to Off. Never listed: rejected
exp_compact_hit_state / exp_alpha_metadata_cache / exp_water_empty_tiles / exp_alpha_spill, perf_* keys, diagnostics and
policy-owned knobs (rt_trace_scale, exp_omm, exp_vt_alpha_masks, rr_post, exp_water_compose_shift,
exp_hair_pool_trim, exp_ls_exact_k, exp_hit_reference, exp_current_sun).
Help & support owns setup, compatibility, memory, overlay/comparison help, notices and one local-report entry. Keep Ctrl+Shift+F9; omit experimental F10 instructions.
Play saves settings first in both renderers; dependency reviews may be cancelled and failed saves keep edits and prevent launch.
One compact header shows version, preset, GPU and mod-file presence. Installed files do not confirm runtime activity.
Costs appear once: supported measurements after descriptions, estimates and separate-pass hair timings in details. Unknown costs and memory badges are omitted.
Dependency dialogs name actions: Enable requirements / Adjust options / Keep editing; Apply changes / Cancel.
Selecting a choice turns on the options it needs at once, recursively, all or nothing (`settings::raise_needs`;
status "Also turned on: ..."); a need that is hidden, custom, experimental without opt-in or unsupported raises
nothing and the card keeps showing it. Turning a prerequisite off still lowers its dependents at Save.
Apply labels: “Applies after saving”; “Applies after saving; may briefly pause”; “Restart the game to apply”.
The native report preview uses the default inclusion set and has no issue field or inclusion checkboxes; WebView provides both.

Automatic release policies (also after stale user INI layering):
- `rr_post=1`; `rt_trace_scale=1.0` in all presets. Half is developer only.
- `rt_hair_pool_trim=1` whenever stock hair capture is selected; otherwise 0.
- OMM/dynamic OMM/fine LOD and offline masks On only for High or Ultra + Full geometry.
  High qualified by rel2-smoke-high-s8 (2026-11-04 12:13). Unsupported hardware/missing masks retain native fallback.
- Water camera correction follows water; wf9 qualified 08:59 with 17 gates, static unchanged,
  turning hits about 6.7% -> 17%; compose 0.154 ms absolute.
- Near/Far writes material/instanced/wind capture, direct/sun bounce and emission mode 1.
- Box On writes `ls_box=2`, `ls_box_honour_shadow=1`; obsolete mode 1 migrates to On.
- Stale Far + reflections migrates to Far and Tilesets to stock hair. Raw `ls_shadowed=1`
  migrates independently of complete choice matching: to 0 for Fast (`ls_samples=1`),
  to 3 for Full (`ls_samples=0`), preserving whether local shadows are enabled.
- Exact-k k1 (`ls_k=1`, `ls_rest_samples=1`) is an always-on qualified Ultra optimisation
  when Full local shadows and ray-traced geometry are active; there is no release user control.
  Lower presets stay Off: the approved Full-path evidence does not qualify them.
  User decision 2026-11-04: S7 inn 0.717 -> 0.555 ms (-23%); pre-post scene energy
  -0.38/-0.34/+0.12%, within OFF spread 0.89%. Stale raw k/rest overrides cannot disable or change it.
- Hidden candidates and perf_* overrides are reset; unknown unrelated keys remain preserved.
- Release profile no longer pins `ls_box` or `rt_emissive` Off.
- `gi_rt_pipeline=1` in every preset (user decisions 2026-11-08; live inn/camp GI trace about -2 ms, energy unchanged).
  A no-op without the hit-GI trace (Off-screen bounce light Off, SHaRC); compute fallback without the device features.
  `gi_rt_pipeline_reorder=1` (by hit kind) in every preset, user decision 2026-11-08 ("Yes, turn it on"): camp -1.5 / -1.6 ms
  in two runs, inn -0.8 ms in one clean window (the committed two-scene rule is not met: the inn's scene thread hits wait
  timeouts in its OFF windows). Needs the ray tracing pipeline trace; a driver without reordering gets no hint.

Controls hidden from the three main pages (Experimental opt-ins remain available there):
- `rt_trace_scale`, `water_surface_fresnel`, `exp_omm`, `exp_vt_alpha_masks`
- `exp_water_hit_shade`, `exp_water_hit_cost`, `rr_post`, `exp_compact_hit_state`
- `exp_alpha_metadata_cache`, `exp_water_empty_tiles`, `exp_water_compose_shift`, `exp_hair_groom`
- `exp_hair_groom_primary`, `exp_hair_fibre`, `exp_hair_fibre_gi`, `exp_hair_groom_crowd`
- `exp_hair_groom_shadow_t`, `exp_hair_primary_passes`, `exp_hair_groom_tips` (strand order/current/frame tag/LOD keep/
  hold/world tangent are part of `exp_hair_groom_primary` On since 2026-11-08; no knob of their own. On also writes
  the user-tested set: rt_hair_groom, shadow/AO transmittance, cascade_drop, fibre/fibre_gi/fibre_spec/fibre_mapped,
  rr_hair_guides, rt_hair_physical_origin; restart required)
- `exp_hair_pool_trim`, `exp_hair_physical_origin`, `exp_alpha_spill`, `exp_hit_paks`
- `exp_hit_reference`, `exp_ls_exact_k`, `exp_current_sun`
Hidden choices: local_shadows Full + shadowed lights; box_lights legacy Shadowed;
exp_hidden_surface Far + reflections; exp_native_coverage + Tilesets.
Fresnel stays Experimental and Off in presets: invisible on open water to the user.
Strand hair is a post-release update on Experimental; stock visible hair ships.
One-ray Ultra GI and Tilesets are being qualified; retain two rays and hide Tilesets until accepted.
Temporal Ultra promotion is accepted by visual verdict, not a statistical/energy pass.
Rejected/Off: three GPU candidates, Medium Half, water bits 2/4, glint, alpha spill
(no S8 overflow), GI guards, current Sun and IP grass. SHaRC is rejected unless matched inputs exist.
Rejected controls stay excluded from Experimental; diagnostic strand controls stay developer only.
Only RTX 5090 tested. Manual launcher acceptance is DPI only; keep work lean.
Exact-k selects existing LS_K requests in Ultra; no shader source, define, specialization ID
or variants catalogue changes; request delta 0. Non-Ultra keeps the default path.
Copy/layout changes do not change keys, values, presets, dependencies or apply transitions. The only flow change is Play saving first; blur smoothing moves to Surfaces.
