## Strand shadow transmittance, C2 (branch claude/hair-shadow-transmittance, offline verified, live unqualified, 2026-11-04)
- Key rt_hair_groom_shadow_t stays off. Specialization 1026, kSpecIdHairGroomShadowT, replaces the dormant
  RT_GROOM_SHADOW_T_REFERENCE copy. At 0 the original rt_groom_trace first-hit expression runs; at 1 the
  stochastic d_f * prod(a_f) visibility runs. GI and native water carry the value in their pipeline cache key.
- Production and tools/hair_shadow_gpu_check.hlsl now use the same hp_crossing helper (8 recent candidates),
  replacing C2's separate 4-crossing implementation. The primary colour samples and measured a_f publication remain.
- No shader requests added (delta 0), no ABI change. Experimental prewarming includes the new constant.
- tools/hair_shadow_check.py checks ordinary requests against main without overlays or reference defines.
  Only raw DIFFERENT requests can use strict retirement/default equivalence: baseline IDs 1017-1019 = 0,
  ID 1026 = 0 on both sides. All other surviving specializations remain variable. Unit checks cover nonzero
  C2 defaults, changed arithmetic, invalid modules, entry mismatch and preservation of other specializations.
- Full build passed: 147/147 CTests. Unchanged water_surface_shader_test: 16/16 OFF accepted (1 raw,
  15 strict equivalent), 2/2 raw ordinary accepted (1 raw, 1 equivalent), 16/16 production modules validated.
- Ordinary proof vs main 9cb1e3a: 113/255 requests byte-identical, 142 equivalent at default; 0 failed.
- Clean 009f60f water rerun passed with the same counts and zero DXC compiles.
  The first 009f60f run was INVALID at the source-freeze check after a concurrent prewarm-header edit.
- Hair L2 keeps every participation, energy, producer and cost gate, hold 90 and 300-frame windows. Two PNG
  pairs per save (base/shadow, base/tips), four cost windows and one combined candidate warmup.
- Dialogue1: 336.30 s plan + 216.86 s readiness + 120 s load = 673.16 s. Campsite: 284.60 + 210.20 + 120 =
  614.80 s. Both under 780 s including allowance; two-launch total 21.5 min, 1,780 MB estimated.
- Dryrun passed all checks with MaxMinutes 26 (aggregate). Jobs remain outside watched queue inboxes.
- Current GPU bench passed: 100% of 3000 ray crossing counts match CPU; transmitted trials 174623 vs 175064.4
  expected, sigma 353.2 (1.25 sigma error). No live run or measured GPU cost; not a quality pass.
- User check after authorization and hair safety prerequisites: Test Dialogue1 then Campsite, skin/neck under
  hair in shadow pair, tip fade in tips pair, float pre-post energy and GPU pass ms in the retained gates.
- No commit, game launch or queue submission performed.
