## Water hidden-hit reflections (branch rt-reflections-offscreen, READY_FOR_LIVE, 2026-11-03)
- Resolved bundle-20261103 in place; Git metadata read only, no staging or commit.
- rt_water_hit_shade remains 0 in all presets; Experimental control has explicit apply metadata.
- Replaced provisional 1100 with registry SpecId 1016; cascade 1000, hair 1008/1010,
  edge 1014 and compose shift 1015 share the async pipeline specialization map/cache key.
- WATER_HIT_SHADE_LAYOUT adds material bindings 21-29, game set 2 and sun set 3.
  Regenerated variants: bundle 147 -> 156 (+9 layouts, no on/off multiplication).
- Compose closest-hit origin and shading hit both use compose_scene_origin; screen H stays current.
  Hit-shading eligibility is flag 64, separate from camera shift flag 32 (CPU regression gate).
- Borrowed lighting remains exact-frame/TLAS only; earlier lighting keeps ordinary compose.
  Unknown materials/light/roof keep the native probe; C + A L - B is unchanged.
- Build succeeded; all 125 CTests passed across full suite + targeted corrective reruns.
  Defaults check and variant generator pass; ordinary pp-diff vs bundle: 235/235 unchanged
  (210 input-identical, 25 preprocessed-identical), zero compiles required.
- watercasc: 19/19 requests, 68/68 comparisons; waterhit: 9/9, 64/64 (both shift values).
  watershift: 17/17 requests, 68/68 comparisons. Initial waterhit OFF gate failed on a
  short-circuit histogram predicate; eager capability-only predicates passed, checker unchanged; controls differ.
- Prepared tools/live_batches/water_offscreen: S8 Water Queens House, Ultra, shift=1,
  static and matched wf9 turns 0/1/0/1 in one visible_unfocused_fg launch. Prediction first.
  14 phases, 225 s, 12 static traces, no float bursts; synthetic dryrun passed.
- Every ON window requires hidden AND offscreen shaded > 0 in both extended stats lines.
  Offscreen is predicted zero (light-grid rejection), so that requested gate is expected to fail.
- Automatic post_gate: static float water-compose energy within OFF noise for BOTH ONs;
  compose/reflection GPU ms median/p90 remain unmeasured. Commit prediction before live.
- In game after separate authorization: S8 pool hidden reflections should follow matched turns;
  unsupported hits retain probe. Existing chain 0.53-0.67 ms is not this increment's cost.
- No game launched, real job queued, quality pass, preset promotion or RF bias relaxation.
