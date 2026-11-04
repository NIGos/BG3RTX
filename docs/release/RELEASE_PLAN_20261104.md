# Release plan, 2026-11-04 (stock hair)

User direction 2026-11-04: release with stock hair, all other qualified features, backlog closed, launcher limited to meaningful options, qualified optimizations always on. Strand hair continues separately.

Update after the plan: go2-s8 08:12 rejected rt_compact_hit_state, rt_alpha_metadata_cache and rt_water_empty_tiles (image-detail gate fails; savings 0.53 / 0.11 / 0.19 ms). They stay hidden and off. Medium half resolution saves 0.25 ms only: Full everywhere.

Ship stock on-screen hair. Strand development continues separately. Keep qualified features; close rejected items and explicitly defer unqualified candidates rather than treating implementation as qualification.
Sources: [settings model](common/settings.h), [defaults review](docs/release/RELEASE_DEFAULTS_REVIEW.md), [offline status](docs/release/RELEASE_OFFLINE_STATUS.md), [launcher QA](docs/release/LAUNCHER_QA.md), progress, handoff and supplied triage. `PRODUCT_REVIEW.md` is absent.
Current code and supplied steering supersede older documents: temporal post is promoted to Ultra; hair resolve/static-only cascade are removed; CPU experiments are hidden; Ctrl+Shift+F9 controls master RT.

**1. Fate of every current `kKnobs` entry**
USER OPTION rows give the final label, page and choices. ALWAYS ON means automatic when prerequisites apply, with hardware fallback; it does not turn Low into Ultra. HIDDEN means unavailable in release, including direct bridge requests.

| Knob | Release fate and final presentation | Recorded basis / qualification |
|---|---|---|
| `rr` | USER OPTION — Lighting: **Ray Reconstruction**, Off / On | Existing preset feature; requires RTX and in-game DLSS. |
| `rt_geometry` | USER OPTION — Lighting: **Ray-traced geometry**, Off / Opaque / Full | Existing quality/cost distinction; Full includes cutouts. |
| `sun_shadows` | USER OPTION — Lighting: **Sun shadows**, Screen-space / Ray-traced / Ray-traced + hidden roofs | Qualified roof path; +0.3–1.3 ms reference cost. |
| `gi` | USER OPTION — Lighting: **Global illumination**, Off / Screen-space / Ray-traced / Ray-traced High | Retain Ultra’s two rays; reducing them lacks valid flicker qualification. |
| `rt_trace_scale` | ALWAYS OFF/HIDDEN — hide Half; use Full automatically | Half saves ~0.184 ms exploratorily but lacks quality gates. NEEDS-QUALIFICATION before exposure: `gpu_opt2` jobs below. |
| `local_shadows` | USER OPTION — Lighting: **Local light shadows**, Off / Fast / Full | Existing preset levels; Fast requires RR. Hide developer Full + shadowed lights. |
| `reflections` | USER OPTION — Surfaces: **Reflections**, Off / Glossy / Standard / Rough | Existing roughness/cost tiers; preserve dependency handling. |
| `frame_generation` | USER OPTION — Performance: **Frame generation**, Off / 2x / 3x / 4x | Capability-gated; Off/on requires restart. No new performance guarantee. |
| `rt_intensity` | USER OPTION — Lighting: **RT lighting strength**, 50–100%, steps of 5% | Appearance blend; **no evidence** of separate quality qualification. Describe its three affected contributions. |
| `fps_cap` | USER OPTION — Performance: **Rendered frame limit**, Off / 30–240 fps | Existing control; clearly distinguish rendered from generated frames. |
| `reflex` | USER OPTION — Performance: **NVIDIA Reflex**, Off / On / On + Boost | FG requires On or Boost; keep dependency review. |
| `exp_water` | USER OPTION — Surfaces: **Water lighting**, Off / Reflections / + Shadows / + Refraction | Qualified M5 chain; historical ~0.53–0.67 ms, not a universal cost bound. |
| `water_surface_fresnel` | ALWAYS OFF/HIDDEN pending owner visual verdict | Automated gates pass; rim +43%, ON variation 13% versus OFF 2.2%; surface FS 0.10–0.15 ms absolute. If accepted, integrate automatically with water; `wsf1` below. |
| `exp_omm` | ALWAYS ON with supported Full geometry and local masks | Camp 1.17 G rays, zero violations, ~1 ms saving; Ultra smoke passed. NEEDS-QUALIFICATION for expansion to High: existing `su1` is Ultra-only; no qualifying High job found. |
| `exp_vt_alpha_masks` | ALWAYS ON with applicable Full geometry and available local masks | Camp energy +0.24% within ±1.95% repeat band. Validate High expansion with the same final-preset qualification; retain native fallback. |
| `exp_water_hit_shade` | ALWAYS OFF/HIDDEN | No shading-mode A/B in combined evidence. NEEDS-QUALIFICATION for inclusion: repair `wos1`; cost-only `wo1` cannot qualify appearance. |
| `exp_water_hit_cost` | ALWAYS OFF/HIDDEN; automatically fold only passing bits after qualification | Sparse reflection ~2.1–2.5 → 0.53 ms; energy unavailable. NEEDS-QUALIFICATION: `wo1`; bits 2/4 lack consistent savings. |
| `exp_hidden_surface` | USER OPTION — Lighting: **Off-screen bounce light**, Off / Near / Far (6 m) | Qualified material/emission/bounce bundle; Far trace +3.2–3.7 ms. Hide Far + reflections: reference ratios failed. |
| `box_lights` | USER OPTION — Lighting: **Box light shadows**, Off / On | On writes qualified mode `ls_box=2`, honour-shadow=1; +0.13 ms, signed energy delta 0.0 within noise. Hide legacy mode 1. |
| `rr_post` | ALWAYS ON — selected route `rr_post=1`; remove order control | Existing default route; alternate order remains diagnostic. **No evidence** qualifying it as a release alternative. |
| `rr_post_temporal` | USER OPTION — Lighting: **Temporal depth of field and bloom**, Off / On | Ultra promotion explicitly accepted by user; +0.152–0.153 ms, ~250 MiB. Statistical quality/energy gates remain inconclusive/failed; do not rewrite that history. |
| `exp_compact_hit_state` | ALWAYS OFF/HIDDEN pending qualification | Replay 36/36 byte-exact; no demonstrated live saving. NEEDS-QUALIFICATION: `gpu_opt1`; fold automatically only if useful and equivalent. |
| `exp_alpha_metadata_cache` | ALWAYS OFF/HIDDEN pending qualification | 3.84 M exact helper checks; no demonstrated live saving. NEEDS-QUALIFICATION: `gpu_opt1`. |
| `exp_water_empty_tiles` | ALWAYS OFF/HIDDEN pending qualification | Exploratory compose saving ~0.156 ms; after-water equivalence missing. NEEDS-QUALIFICATION: `gpu_opt1`, including empty-view evidence. |
| `exp_water_compose_shift` | ALWAYS ON with water, contingent on closing existing gate | Accepted appearance/static delta 0 m; corrected qualification incomplete. NEEDS-QUALIFICATION: `wf9`; exploratory compose +0.009 ms. |
| `exp_native_coverage` | USER OPTION — Lighting: **Hair shadows and occlusion**, Off / Stock hair + scalp | Accepted stock-card coverage; RT +2.3–6.3 ms, GI +3.2–8.8 ms. Hide unqualified Tilesets choice. This does not replace visible game hair. |
| `exp_hair_groom` | ALWAYS OFF/HIDDEN | Secondary strand evidence incomplete; outside this release. |
| `exp_hair_groom_primary` | ALWAYS OFF/HIDDEN | First real swap lost device through released BLAS #552; outside this release. |
| `exp_hair_fibre` | ALWAYS OFF/HIDDEN | **No evidence** of qualified primary appearance/cost; strand-only. |
| `exp_hair_fibre_gi` | ALWAYS OFF/HIDDEN | Strong offline arithmetic, incomplete acceptance/live cost; strand-only. |
| `exp_hair_groom_crowd` | ALWAYS OFF/HIDDEN | **No evidence** of qualified crowd image/cost; strand-only. |
| `exp_hair_groom_shadow_t` | ALWAYS OFF/HIDDEN | 3,000 crossing checks pass; live qualification incomplete; strand-only. |
| `exp_hair_primary_passes` | ALWAYS OFF/HIDDEN — diagnostic, inert with strands disabled | No qualified release use; do not confuse choice index 0 with raw pass-mask 0. |
| `exp_hair_primary_order` | ALWAYS OFF/HIDDEN | Diagnostic; no completed live verdict; strand-only. |
| `exp_hair_groom_tips` | ALWAYS OFF/HIDDEN | Offline shares exact; live appearance/cost missing; strand-only. |
| `exp_hair_pool_trim` | ALWAYS ON whenever stock-hair capture applies | Mesh pool −26–50%; accepted; numeric GPU timing **not measured**. |
| `exp_hair_physical_origin` | ALWAYS OFF/HIDDEN | Guard changes invalidate historical qualification; retain developer work. |
| `exp_alpha_spill` | ALWAYS OFF/HIDDEN | 105 CPU checks; **no evidence** of nonzero live rescue/cost; no qualifying job found. |
| `exp_hit_paks` | ALWAYS OFF/HIDDEN | Camp publication 0%; incompatible cohort. S6 job adaptation needed; no qualifying publication/image job found. |
| `exp_hit_reference` | ALWAYS OFF/HIDDEN — developer diagnostic | 334 CPU checks; **no evidence** of two-surface runtime qualification. |
| `exp_ls_exact_k` | ALWAYS OFF/HIDDEN | Inn −23%, energy within OFF noise, but scoped evidence and explicit prior developer-only decision; no general preset qualification. |
| `exp_current_sun` | ALWAYS OFF/HIDDEN | Samples disappear into scene-unready fallback; no current qualifying job found. |

REMOVE: no remaining `kKnobs` entry is proven dead. Keep already removed `rr_hair_resolve` and `exp_cascade_static_only` absent; retain `perf_*` only as hidden, default-0 developer keys because active code is shared.
Finish removal of `rt_water_rf_edge` and `rt_water_rr_hitdist`; the other three rejected/null water keys are already absent. Preserve unconditional NaN/Inf and other established correctness fixes.
Resolve release pins by **deleting** `ls_box=0` and `rt_emissive=0` from `data/release.ini`, then regenerate `kReleaseDefaults`. Do not globally force either On.
The pins are redundant base values, not immutable vetoes: the later user preset overrides them. Keep Ultra `ls_box=2`; Near/Far must write `rt_emissive=1` and `rt_hit_emission=1` with all material prerequisites.
Release policy must also neutralize stale experimental user overrides; current preset selection preserves hidden controls. Merely hiding cards or appending base-profile zeros is insufficient.

**2. Minimal launcher**
Lighting contains the USER OPTION controls listed above; Surfaces contains Reflections and Water lighting; Performance contains FG, rendered frame limit and Reflex.
Advanced contains **no release controls**: remove its empty navigation entry and Experimental disclosure. Put compatibility, memory information, overlay troubleshooting and local reports in Help & support.
Keep four globally visible presets and a Custom indicator. Use **Ultra**, without an RTX-model guarantee.

| Preset | Retained feature defaults |
|---|---|
| Low | RR/RT geometry off; screen-space sun/GI; local shadows/reflections/water/bounce/stock-hair capture off. |
| Medium | RR, opaque geometry, RT sun/GI, Fast local shadows, Glossy reflections; Ultra extras off. |
| High | RR, Full geometry, RT sun/GI, Full local shadows, Standard reflections; Ultra extras off. |
| Ultra | Full geometry; hidden roofs; two-ray GI; Full local/box shadows; Rough reflections; water + refraction; Far bounce/emission; stock hair/scalp; temporal post On. |

All presets use Full trace resolution until Half qualifies; Reflex On and supported FG 2x remain current defaults; cap Off except Ultra 58 rendered fps.
Apply trim automatically when applicable; apply OMM/masks across Full configurations only after the High extension gate. Unsupported hardware/local data uses the existing fallback.
Keep restart badges, dependency previews, saved/pending state and keyboard focus. Remove experimental F10 instructions from release copy; retain Ctrl+Shift+F9 master comparison.
Use existing VISUAL_STYLE assets, flat readable panels and system high-contrast colors. Replace stale sample cards/costs and reconcile native fallback with WebView behavior; do not redesign the renderer.

**3. Release blockers, candidate gates and decisions**
`B/` below means `tools/live_batches/`. Minutes are recorded triage/README estimates including loading, not guarantees. Every run needs fresh explicit steering; none is authorized by this plan.

| Required before shipping the selected configuration | Scene / minutes | Observable |
|---|---|---|
| `B/water_flicker_wf9/jobs/wf9-s8-compose-shift.json` | S8 / ~7.0 | Static delta <0.01 m; nonzero samples; held-Q/E coverage and float-energy gates. |
| High adaptation of `B/smoke_ultra/jobs/su1-ultra-s8-fg2.json` | S8 / **no recorded duration** | Qualify OMM/mask fold-in under actual High settings; no existing High receipt. |
| Final `B/smoke_ultra/jobs/su1-ultra-s8-fg2.json`, using release profile and stock hair | S8 / ~5.5 | Every-window participation, zero overflow/timeouts/OMM violations, water active, driver-budget VRAM, GPU ms and master toggle. |
| Stock-hair dialogue/startup acceptance | Dialogue1–3 / **no current executable hair_qual jobs or duration evidence** | Cards remain visible; scalp/face/neck shadows, first-enable compilation, no strand activation/device loss. |
| Memory/transition, Act 2/3 and RTX 30/40 acceptance | Multiple scenes / checklist requests 2–4 hours | VRAM growth, capacity, cache/log rotation and FG stability; no completed receipt. |

The existing `long_session/ls1-long-session-camp-fg2.json` is ~43 minutes in one launch and violates the 15-minute cap. Redesign the acceptance protocol before execution; shortened runs alone cannot prove multi-hour retention.
Optional candidates become blockers **only if selected for inclusion**: `gpu_opt1/go1-camp.json` ~12.3 min and `go2-s8.json` ~7.5 min; identity, float energy, detail and useful cost saving.
Half-resolution inclusion needs `gpu_opt2/go2-s8-medium.json` ~4.8 min and `go2-camp-high.json` ~6.3 min; energy/reflection/detail gates. Otherwise ship Full and close Half as deferred.
Hidden-water inclusion needs repaired `reflections_offscreen/jobs/wos1-s8-fg2.json` (12 lint findings; 183 s phases plus load/readiness), then `water_offscreen/jobs/wo1-s8-hit-shade.json` ~11.5 min for every-ON energy, coverage and zero truncation.
Fresnel requires the retained-rim visual verdict; repeat `water_surface_fresnel/jobs/wsf1-s8-interface-column.json` ~5.6 min only if needed, then final smoke.
Generated rows/GI guards stay off: `m4_generated` jobs have 9 lint findings each; `qual_stage4` jobs have 46/42. Stage-4 screening cannot satisfy the original transient gate.
Pak publication, spill, current cascades, hidden-reflection AO, SHaRC, IP grass, lava/HeightGradient/HLOD, texture publication, AS fragmentation and M5.5 follow-ups lack complete qualification. Close each release ticket with explicit fix/reject/defer disposition; do not claim the rendering backlog solved.
Strand hv6/hv5/crowd/L2/GPU-AV work is excluded from release blockers.

Offline closure: reconcile defaults/schema/generated tables, specialization state and effective preset layering; enforce release experiment policy; finish two water-key removals; verify old-user migration and prerequisite writes.
Update README/settings documentation to the final controls, stock hair, tested versions and honest GPU/VRAM limits; remove obsolete Experimental and strand activation guidance.
Source diagnostics/notices wiring already passes; inspect the **final archive**, files.txt, branding/fonts/licenses and runtime DXC. Keep symbols private and excluded SDK/tools absent.
Complete disposable installer shortcut, locked-file and interruption checks; fresh-PC/no-VULKAN_SDK install, Steam launch, update/uninstall acceptance; manual IncludeSave confirmation using synthetic data.
Complete actual WebView zoom 100/150/200%, multi-monitor DPI, high contrast, Narrator, keyboard/focus and Apply/restart acceptance. Offline DOM checks do not close these.

Owner decisions:
- Which project licence, NVIDIA notification action, asset-data redistribution policy and Microsoft runtime terms are approved? SHaRC remains off and its headers excluded unless explicitly resolved.
- Accept Fresnel’s rim appearance, or close it hidden? Keep temporal promotion with its recorded evidence limitations?
- Approve explicit deferral of the unqualified non-hair backlog, or require qualification before this release?
- What hardware/content support scope is acceptable after memory, Act 2/3 and compatibility acceptance? Current evidence does not qualify broader claims.
- Resolve ambient ratio 1.44 and water shadow/glint appearance; emission remains mode 1 unless separately changed. Approve the final archive/upload only after review.

**4. Parallel work streams and dependencies**
- **A — Settings/presets/profile:** owns `common/settings.h`, `bg3rtx.ini`, `data/release.ini`, generated settings/schema and settings tests. Implements automatic policies, release filtering/override migration, choices and dependencies; publishes the control contract first.
- **B — Launcher:** owns `tools/launcher/ui/*`, `ui_product.h`, `web_bridge.*`, bridge/UI tests and `LAUNCHER_QA.md`. Consumes A’s contract; owns labels/layout enforcement and native/WebView parity. A alone edits central knob metadata.
- **C — Packaging/docs:** owns `scripts/package.ps1`, `scripts/release/*`, installer/package tests, release README/checklists/notices. Consumes A’s final defaults and B’s final UX; handles owner decisions and disposable acceptance.
- **D — Qualification:** owns `tools/live_batches/*` generators/jobs/plans/predictions and result summaries. Repairs/lints offline, stages outside watched inboxes, then runs the authorized serial batch; reports deciding numbers and disposition.
Start A/B/C preparation and D offline work in parallel; qualify A’s candidate build, feed results back to A, then run final integrated smoke and B/C acceptance against the frozen configuration.
Use disjoint branch progress fragments; coordinator owns shared CMake registration and final handoff/progress folding. Serialize heavy DXC; keep builds out of live cost windows; merge only through `merge-green`, outside live runs.