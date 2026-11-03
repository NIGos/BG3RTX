## Surface rejection diagnostics (branch water-fresnel-zero2, READY_FOR_LIVE, 2026-11-03)
- Second live: clean 0 despite 120/120 patched draws and 99.53% owned. No offline proof of a record/spec mismatch; no speculative shading fix.
- w7 reason bits report independent foam colour/F0/roughness vetoes, foam-texture and edge/flow-mask sources, native/missing marker, layout and FS/compose specialization; no stride, ABI or request delta.
- Counters are device-local/group-reduced; push flag independent of SpecId 1025 exposes an unset compose specialization. Build and 6/6 targeted CTests pass; CPU 1,237,748 checks and all patched FS audits/spirv-val pass. Compose: 16/16 arithmetic-OFF and 2/2 raw ordinary identical to HEAD; 16/16 production validated, all counter contracts pass.
- Predict ON: clean owned may remain 0, marked owned > 0, at least one foam veto > 0; correctly wired layout/spec-off 0. Native marker alone cannot identify its family.
- No game launched or git metadata changed; key remains off. S8 pool rim A/B and measured compose GPU cost remain pending.
## Coverage-layer replacement (water-fresnel-zero2, READY_FOR_LIVE, 2026-11-03)
- Supersedes clean-only design: authored BridgePools/Fountain c floors 7.96214e-8/1.19432e-7; exactly-zero share 0%, all 27 foam/flow mips decoded (docs/evidence/water-foam-coverage.md).
- Native foam c plus independent clean BRDF (1-c); coefficient sum 1 for every coverage, exact endpoints, full-float shading; existing counters now coverage bins and weighted ownership, runtime finite/domain/8-epsilon partition check.
- Core/layer build and 2/2 final water CTests pass, 1,256,907 CPU checks/all seven FS forms validated; 148/148 CTests covered (shader snapshot invalidated by final FS edit, cached rerun passes); OFF 16/16, raw 2/2 identical, production 16/16 valid, counter contracts pass. Key off; ABI/stride/variant request delta 0.
- Prepared single unqueued wsf1: 127.4 s plus load, real Ultra static 0/1/0/1 float shots and held E/Q; per-ON weighted ownership > 0/invalid 0/partition, all eight post-water float shots required; lint/fake dryrun pass, about 490 MB.
- No game or git metadata writes. Inspect S8 shallow rim/foam; model mean may change. Dual FS forward cost unmeasured and excluded from existing compose timer; report that limit and compose GPU ms. Lives remain paused.
