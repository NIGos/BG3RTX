## Occluder diagnostic + main migration (branch worktree-agent-a0c9be1ab336eae1a, READY_FOR_LIVE, 2026-11-03)

- Resolved main into the branch files only. No Git metadata write, staging, commit, merge command or game launch.
- Preserved main migration, generated pool rows, launcher/VRAM tests, box cascade-entry fix and dialogue jobs.
- rt_occ_stats stays 0 by default. Full-box diagnostic uses reserved SpecId 1011 (RT_SPEC_OCC_STATS marker).
  Set 4 binding 1 is always bound to the existing device-local counter buffer; declarations remain unconditional.
  Ordinary and diagnostic Pipes specialize the same module to 0/1 and include that value in the slot key.
- Diagnostic flag uses bit 13, separate from main's two-sided pool flag at bit 12. No ABI change.
- ls_box_cascade_old uses existing cascade constant 1000 to restore the old box mask for A/B.
- Standalone sun/AO diagnostic retains its fixed RT_OCC_STATS implementation (only dispatched when enabled).
  The checker retains the define reference; no new layout-changing on/off define was introduced.
- Rebuilt shader_variant_test and regenerated variants.txt: 165 -> 167 variants, 53 -> 54 GI bases.
  Validator: 284 assertions. Settings generator reports up to date; C++ build passed.
- CTests: 119/119 passed (275.04 s), including occ_stats, char_energy, settings and host atomics lint.
- pp-diff main f0b0ccbb2733: 241/241 = 57 identical inputs + 184 pp-same; 0 compiles required.
- First cascade check rejected (49/50): sun replica only set g_rt_cascade_ray under the old define.
  Fixed its guard to include RT_SPEC_CASCADE; checker comparison rules unchanged. Final regression passed.
- Final sample 8, requests: cascade 50/50, guards 26/26, hitfam 23/23, gifar 12/12,
  hitinst 23/23, native 62/62, rfgi 12/12, wateredge 9/9. All assignments matched.
  Controls in that order: 1 / 436 / 17,633 / 98,934 / 52,940 / 54,005 / 129,638 / 7,302 lines.
- New occ key, sample 16: 2/2 requests (all 4 assignments), control 64,139 lines; both values match define references.
- Key off: all 251/251 merge-parent requests byte-identical, including 10 replaced box requests.
  New full-box modules off are equivalent after specialization/freezing/optimization (occ + cascade gates).
- Main moved during checks to 6e69f90 (new water migration, 145 variants): symbolic-main identity was confounded
  (209/231); reran against recorded MERGE_HEAD f0b0ccbb2733 (165 variants), 251/251. Newer main is outside this merge.
- Logs: merge-build.log, merge-ctest.log, merge-pp-diff.log, merge-spec-*.log, merge-identity.log.
- rt_occ_stats reports character sun/local/box/AO occluder kinds, nearest distance and darkening per window.
  Held tagging requires the layer rebuild/restart. dump_preview_f32 supplies character-mask float energy.
- Live evidence/GPU ms remain unmeasured. Dialogue1/2 camp and Dialogue3 port jobs are preserved outside inboxes.
  Check diagnostic cascade share with ls_box_cascade_old=1 versus 0 in the prepared dialogue_shadows batch.
  No quality pass or preset promotion; next live batch requires explicit user steering.
