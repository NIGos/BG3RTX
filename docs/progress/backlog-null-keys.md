## Retire measured water experiments (branch backlog-null-keys, verified offline, 2026-11-03)
- Correction: wf2-wf7 whole-water flicker was dominated by native glints (wf8).
  It cannot decide reflection-key quality. Audit verdicts now explicitly say unknown;
  re-measure rt_water_rf_edge and rt_water_rr_hitdist with the wf8/wf9 reflection-only metric.
- Restored both keys from bundle-20261103: defaults 0, runtime and shader paths,
  SpecId 1014, edge helper/tests, RR hit-distance output, live batch set entries
  and the spec-cascade edge toggle windows. Neither key is qualified for presets.
- Kept removal of rt_water_rr_mv, rt_water_fg_mv and rt_water_glint_aa:
  MV verdicts use parity; glint is validly judged by the glint-dominated metric.
  Removed their settings, branches, resources, shaders and job overrides; ABI 26 retained.
- IP grass remains unknown/off; generated rows and compose shift await qualification.
- shader_variant_test --write: 147 -> 135 requests (-12); no reflection-key request removed.
- shader_identity --pp-diff bundle-20261103: 221 requests, 212 identical inputs,
  9 pp-same, 0 changed inputs after preprocessing, 0 failures, 0 compiles needed.
- spec_cascade_check --key wateredge --sample 8: all 7 available requests passed;
  28 equivalence comparisons, 0 failures; 42 cached shader entries, 0 compiles.
- Full build passed (15 s); all 125/125 CTests passed (105 s), 0 failures.
- gen_settings_defaults --check and git diff --check passed. GUI schema unchanged.
- No game or live batch launched; reflection-only quality and cost remain unmeasured.
  No new GPU pass or default-path change. No Git metadata writes or commit.
