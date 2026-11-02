## Variant migration S3 finish (branch variant-migration, offline verification passed, 2026-11-02)
- WIP base: 32db61a. No commit, merge, Git metadata writes or game launch in this session.
- Constants: 1008 tileset, 1009 physical hair origins, 1010 scalp/hair. RT_OCC_STATS remains excluded.
- Existing S3 host work always binds layer tileset samplers and core b50 hair origins (dummy when off).
  Reflection b12 stays omitted for the 32-descriptor budget. No ABI change.
- rt_shadow first mismatch: specialized debug loop ordered the near_kind/cand_n uint phis differently
  (body lines 979-980, then their increment/compare uses); 5 differing lines with hair off.
- Fix: keep the procedural debug loop inside RT_HAIR_ON, followed by the original triangle-only loop
  in else. Hair-first order preserves both specialized dataflows. Ray flags and checker unchanged.
- Full shadow coverage also found a missing tileset-off material-kind check in sun_caster_visibility;
  restore the original kind 1..4 bound in else (tileset-on keeps 1..5).
- Rejected intermediate: triangle-first loop split fixed hair-off but changed hair-on; full coverage caught it.
- Final-source build: 101/101 CTests passed (49.32 s). Variants: 170 of 53 bases; delta 0 vs WIP,
  -462 vs pre-S3 632. pp-diff main 8464d12: 140 requests, 37 identical inputs + 103 pp-same, 0 compiles.
- Full rt_shadow native check: 4/4 requests, all 24 assignments, 0 failed; control differs 28,585 lines.
- Native sample 16: 69/69 requests, all 352 assignments, 0 failed; control differs 77,160 lines.
- Earlier keys, sample 8: cascade 44/44 (88 assignments), guards 26/26 (52), hitfam 23/23 (92),
  gifar 8/8 (16; all requests), hitinst 23/23 (115); 0 failed. Controls differ 1 / 436 / 17,633 /
  98,934 / 52,940 lines respectively.
- Compiled interfaces: 256/256 requests, 313 entries; GI/reference both tileset layouts and 20 RF modules
  pass the actual SPIR-V descriptor checkers. Logs: s3-finish-*.log. Finished at 23:35 local, before 01:00.
- No live quality claim or GPU cost measurement. S4/S5, final --all and merge-green remain integration work,
  followed by an explicitly authorized scc1 batch.

## Earlier migration checks (branch variant-migration, sampled offline checks passed, 2026-11-02)
- G 87df2ce: constants 1001/1002, guards 34/34 and S8 replay bit-identical. S2a a332afc: hitfam 27/27.
- S2b: guard far radius/base radius, weights and classification with 1007; gifar 0/20 -> 20/20.
  Far-on loads needed non-far initialization in else. hitinst 31/31 and cascade 52/52 at sample 16.
- S2c: instanced 1003 with wind/IP grass 1004/1005. S3a cb79a4a: always-bound layer b19 samplers.
- WIP checker canonicalization compares every instruction, type, operand and edge; only parallel phi
  declaration order by type and intact incoming pairs are normalized. Five CPU tests cover this behavior.
