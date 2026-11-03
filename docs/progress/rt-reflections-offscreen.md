## Water hidden-hit reflections (branch rt-reflections-offscreen, READY_FOR_LIVE, 2026-11-03)
- Added `rt_water_hit_shade=0`, off in every preset, with an Experimental GUI control.
- Both the geometric-water diagnostic and the forward-water compose trace shade supported tier 2/3
  hits through `native_hit_shade`; unsupported materials, lighting and sun keep the probe.
- Compose retains the exact `C + A L - B` replacement. No second Fresnel or exposure scale is applied.
- Borrowed game/sun descriptors are used only for their exact lighting frame and TLAS identity.
  Earlier lighting and material-publication failures select a cached ordinary compose pipeline;
  the existing water reflections, shadows and refraction continue. Both forms are prewarmed.
- Forward-water light lists must equal the lighting sources before hidden-hit shading uses them.
- Device-local group-reduced stats extend the existing water lines: attempted, hidden/offscreen shaded,
  and material/light/sun/invalid rejections. CPU readback preserves every new compose counter.
- Provisional SpecId 1100 in common/shader_variant.h (migration is absent at HEAD/main 772a74d).
  `WATER_HIT_SHADE_LAYOUT` is a descriptor-layout capability, not the on/off key. Request delta +18.
- Offline: core build passed; settings 2428 checks; variants 3059 checks; water compose 615533 checks;
  host-atomics lint and generated defaults passed. New CPU gates cover frame/TLAS fallback and readback.
- Shader gate: 57/57 ordinary water reflect/compose/glint requests byte-identical to 772a74d;
  18/18 capability requests compile and validate (36 entry outputs, 20 unique modules).
- S8 replay NOT_COVERED: opaque GI pack lacks water TLAS, forward FS records and compose targets.
- Native hit shading still rejects hits outside the camera light grid. This increment cannot resolve
  arbitrary objects behind the camera; missing material/geometry coverage also remains.
- Live GPU cost and post-water energy are unmeasured. No game launched, job queued or quality pass claimed.
  Existing water chain reference 0.53-0.67 ms is not the new path's incremental cost.
- In-game check after explicit batch authorization: S8 Water Queens House port, still + turn in one launch,
  OFF/ON/OFF; see tools/live_batches/reflections_offscreen, predicted.json before the unqueued job.
- rf_hit_shade bias threshold is unchanged; no preset promotion. Candidate future placement Ultra only.
- Git metadata was read only; no commits, merges, branch changes or pushes.
