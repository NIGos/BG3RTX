## Generated hit-material rows (branch hm-recipe-rows, READY_FOR_LIVE, 2026-11-02)
- Full original progress details are preserved verbatim in TRACK_D_REFLECTIONS.md section 14,
  under "Recipe-row branch progress details (verbatim)".
- `rt_hit_material_generated=0`: generated rows stay off. Generator supports 11 audited rows,
  including two-sided pool rows; HLOD two-sided 3b346b42 remains rejected after the schema audit.
- Initial offline checks: 2961/2961 shader requests byte-identical, 100/100 CTests.
- First live result: camp qualified share 26.9% -> 29.2% (+2.3 points), inn +0.12 points;
  energy -0.12% / -0.10% within repeat bands, GI trace +0.58 / +0.03 ms, overflow 0.
- Camp share gate was invalid (window length); generated two-sided clip checks were bit-exact.
  Gain was below prediction; keep off. Updated m4_generated rerun needs explicit user steering.
- IP_Base_VT capture skip requires an already noted unique alpha source before allocation.
  Expected saving: 83 unmapped camp draws, deferred stream previously 57 -> 93 MB.
  Generated / IP grass off preserves other audited capture decisions. Live cost unmeasured.
- Extended native generated-row tests cover source-key eligibility and HLOD rejection.
  Items 3 and 4 originally awaited build verification; see the perf-bundle merge report for results.
