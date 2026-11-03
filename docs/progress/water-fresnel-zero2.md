## Surface rejection diagnostics (branch water-fresnel-zero2, READY_FOR_LIVE, 2026-11-03)
- Second live: clean 0 despite 120/120 patched draws and 99.53% owned. No offline proof of a record/spec mismatch; no speculative shading fix.
- w7 reason bits report independent foam colour/F0/roughness vetoes, foam-texture and edge/flow-mask sources, native/missing marker, layout and FS/compose specialization; no stride, ABI or request delta.
- Counters are device-local/group-reduced; push flag independent of SpecId 1025 exposes an unset compose specialization. Build and 6/6 targeted CTests pass; CPU 1,237,748 checks and all patched FS audits/spirv-val pass. Compose: 16/16 arithmetic-OFF and 2/2 raw ordinary identical to HEAD; 16/16 production validated, all counter contracts pass.
- Predict ON: clean owned may remain 0, marked owned > 0, at least one foam veto > 0; correctly wired layout/spec-off 0. Native marker alone cannot identify its family.
- No game launched or git metadata changed; key remains off. S8 pool rim A/B and measured compose GPU cost remain pending.
