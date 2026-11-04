## Release backlog outcomes (branch outcomes, recorded, 2026-11-04)

- Water hit shading rejected for release; Experimental. wos1 16:08/17:01 compose refused every pass: light-binding 60/60, input-lifetime 57-60/60, binding 21-22 mismatch; diagnostic water_reflect shaded 2.37 M hidden hits/period. Fix 2f288a51 unchanged.
- Water hit cost bit 1 untested (depends on shading); bits 2/4 rejected, no saving.
- Pak publication rejected: camp 0% (22,686 unique HLOD_VT); S6 cp5 16:16 had 185,373 attempts, 16,616 unique, 0 published, all shader_pair (HLOD) rejects, missing UV 146,875.
- Generated rows rejected: camp 23.455% -> 23.314%, inn 23.671% -> 23.070% within OFF spread; GI +0.299/+0.527 ms. Earlier +2.3 pp not reproduced.
- Alpha spill rejected: as1 S8 15:13 OFF had no overflow, alpha made opaque 0.00.
- Checked existing decisions: three GPU image-gate rejections; Medium Half not shipped (0.25 ms); Fresnel Experimental; strands post-release; wf9 and High OMM/masks qualified; exact-k/temporal Ultra; GPL-3.0-or-later + NVIDIA exception; appearance settled: native ambient (S8 1.44), native S8 water shadow edge/glint and emission mode 1 kept.
- In progress: one-ray Ultra GI, Tileset, roof-leak fix cost (2aa9d0f3), hv9 hair visual. No promotion claimed.
- User check: retain selected release settings; rejected candidates need no new A/B. No game launch or new measurement; docs only, no Git metadata changed.
