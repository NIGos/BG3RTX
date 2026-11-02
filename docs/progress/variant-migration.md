## Variant migration S4/S5 (branch variant-migration, offline verification passed, 2026-11-03)
- Base c24f61c. No merge, commit, Git metadata write or game launch. Finished before 02:00 local.
- S4 retained RT_OMM_SCENE as a define. Alias probe: 3/3 Vulkan 1.3 modules valid, both values 0 differing lines, control 13.
- Full S4 candidate rejected: 18/54 requests matched; 63/108 assignments differed, control 78,906. Reverted its shader/host changes.
- Its first non-tracing control was blind; reran with gi_hit_direct. Permanent OMM reference descriptors would also add work when off.
- S5 kept: RF_HIT_GI constant 1013 and water edge constant 1014. Both keys keep their existing defaults/presets. No ABI change.
- Every RF hit-shade emission/SHaRC layout binds set 6 and the existing 2 KB device-local stats buffer, including GI off.
- Fixes: one shared pixel call avoids a DXC ICE; colour copied outside GI composition avoids a redundant float3 shuffle.
- Water preserves the exact unfaded C + A * L - B grouping, including clipping stats. Initial reassociation failed equivalence.
- Final build passed; CTests 101/101 (124.16 s), including OMM bake identity and host-atomics lint.
- Variants: 150 of 53 bases, -20 vs S3. Full compile: 236/236 requests, 281 entries; both GI/reference layouts and 12 RF modules pass interfaces.
- pp-diff main c17d27d2dd97: 114 requests, 37 identical inputs + 77 pp-same; 0 compiles needed.
- New keys, sample 16: rfgi 8/8 (16 assignments), wateredge 14/14 (56); 0 failed. Controls 129,638 / 7,272 lines.
- Earlier keys, sample 8: cascade 44/44 (88), guards 26/26 (52), hitfam 23/23 (92), gifar 8/8 (16), hitinst 23/23 (115), native 61/61 (306); 0 failed.
- Earlier controls: 1 / 436 / 17,633 / 98,934 / 52,940 / 54,005 lines. All controls non-zero.
- Checker AST integrity: block_order, phi_order, canonical, differ and check_one unchanged from HEAD. No matching rules weakened.
- Logs: s45-check-*.log (wateredge-fixed is final), s45-interfaces.log, s45-*-verified.log, s4-alias-probe.log, s4-omm-control.log.
- Offline only; no GPU ms or live quality claim. Next integration: --all, one merge-green, then an explicitly authorized extended scc1 batch.

## Variant migration S3 finish (branch variant-migration, offline verification passed, 2026-11-02)
- WIP base 32db61a; constants 1008 tileset, 1009 physical hair origins, 1010 scalp/hair. RT_OCC_STATS excluded. ABI unchanged.
- Host always binds layer tileset samplers and core b50 hair origins (dummy when off). Reflection b12 omitted for the 32-descriptor budget.
- rt_shadow fix: procedural debug loop inside RT_HAIR_ON first, original triangle loop in else; preserves near_kind/cand_n phi dataflow.
- Restore tileset-off sun_caster_visibility material kinds 1..4 (on 1..5). Triangle-first intermediate rejected by full hair-on coverage.
- Build/CTests 101/101 (49.32 s); 170 variants of 53 bases, -462 vs pre-S3 632. pp-diff main 8464d12: 140 = 37 identical + 103 pp-same; 0 compiles.
- Full shadow: 4/4 requests, 24 assignments, control 28,585. Native sample 16: 69/69, 352 assignments, control 77,160; 0 failed.
- Earlier sample 8: cascade 44/44 (88), guards 26/26 (52), hitfam 23/23 (92), gifar 8/8 (16), hitinst 23/23 (115); 0 failed.
- Earlier controls: 1 / 436 / 17,633 / 98,934 / 52,940. Interfaces 256/256 requests, 313 entries, both GI/reference layouts and 20 RF modules.
- Logs s3-finish-*.log. Finished at 23:35 local. No game launch, live quality claim or GPU cost measurement.

## Earlier migration checks (branch variant-migration, sampled offline checks passed, 2026-11-02)
- G 87df2ce: constants 1001/1002, guards 34/34 and S8 replay bit-identical. S2a a332afc: hitfam 27/27.
- S2b: guard far radius/base radius, weights/classification with 1007; gifar 0/20 -> 20/20. Far-on loads need non-far initialization in else.
- S2b hitinst 31/31 and cascade 52/52 at sample 16. S2c: instanced 1003, wind/IP grass 1004/1005. S3a cb79a4a: always-bound b19 samplers.
- Canonicalization retains every instruction, type, operand and edge; only parallel phi declaration order by type and intact incoming pairs normalize. Five CPU tests cover it.
