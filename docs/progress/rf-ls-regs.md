## RF receiver probe live state (branch rf-ls-regs, READY_FOR_LIVE, 2026-11-05)

- `frame_replay --compile-only --compile-pass reflections|local-shadows` adds explicit production layouts;
  no new replay pass, pack, dispatch or ABI change. Uses cached SPIR-V; Vulkan validation passed.
- `rf_compact_probe_state=0`, raw developer key, no knob/preset; specialization 1031. Request delta 0.
  ON moves the unchanged receiver probe/DFG/occlusion block after hit traversal/shading.
  Same input loads and arithmetic, probe accumulation order, rays, samples and precision.
- RTX 5090 RF request: rt_reflections_hit_shade_em, RT_SPEC_NATIVE/HAIR_ORIGIN/HIT_INST/CASCADE=1,
  RF_SPEC_HIT_GI=1; specs 1000/1003/1004/1013=1, 1008/1009/1010/1020/1026=0.
  Baseline / candidate OFF / ON: 255 / 255 / 245 registers; Shared Memory Size 800 / 800 / 32 bytes.
- Hit GI disabled (1013=0): candidate OFF / ON 229 / 230 registers, shared bytes 32 / 32.
  Core therefore only activates 1031 when rf_hit_gi > 0. No default promotion or GPU-time claim.
- LS request: rt_local_shadows_box_k, RT_SPEC_NATIVE/HAIR_ORIGIN/CASCADE=1; spec 1000=1,
  1008/1009/1010/1020/1026=0: 168 registers, Shared Memory Size 7168 bytes. Unchanged split/reload;
  no LS candidate or spec 1032 added. All runs report Local Memory Size 68719476752 (anomalous),
  so actual spill bytes are unavailable; the Shared Memory Size figures above are driver labels.
- Exactness by construction and ON/OFF specialized SPIR-V review, not a rendered replay:
  4 image-write sites, 8 ray-query sites, 378 integer-atomic-add sites in both; float opcode counts match.
  All 2277 final composition/debug/output/statistics lines match after 8 reviewed substitutions for
  the relocated probe outputs / merge predecessor. Evidence: .scratch/rf-ls-regs/stored-result-review.txt.
- Production request OFF equivalence vs 6763587b PASS: all 256 specialization assignments covered,
  universal symbolic certificate and negative control PASS.
- Full OFF release equivalence vs 6763587b PASS: 214 requests (196 identical, 18 equivalent),
  2150 assignments covered, 18 universal certificates, 30 frozen checks, negative control PASS.
  Only 17 uncached entries compiled; final CPU comparison reused all 484 cached entries.
  Evidence: .scratch/rf-ls-regs/release-final.json and release-final-cached.log.
- scripts/build.ps1 -Test PASS: 184/184 tests (267 s); settings and frame replay included.
  Final core gating adjustment rebuilt with scripts/build.ps1 -Core PASS. All BelowNormal.
- Initial build/CTest interrupted when bg3 restarted; completed tests passed. No game was launched
  by this lane; no queue access or Git write commands. Checked Get-Process bg3 before GPU commands.
- Pending authorized live: inn, hit GI on, same-run OFF/ON/OFF energy/image/cost A/B and Ultra smoke.
  No RF/LS replay pass exists. 245 registers does not cross the next occupancy step; measure cost live.
  Reproduce statistics with tools/frame_replay.md. Changes left uncommitted for the coordinator.

## Compact RF live batch (READY_FOR_LIVE, 2026-11-05)
- opt_r5/compact_rf: inn/S8, 600-frame static OFF/ON/OFF/ON, dev=1, RF hit shading/GI held ON.
- Reflection medians, preceding-OFF pair deltas/verdicts; four separate rrpost slot-2 float exports.
- Frozen lint PASS (2 jobs); 9 additive readiness entries. Registered test: 8/8 PASS; parent/regression checks PASS.
- No launch or queue/Git writes. Readiness and completion within the 300 s cap remain unproven.
