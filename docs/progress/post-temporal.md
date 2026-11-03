## Temporal DoF/bloom replay (branch post-temporal, READY_FOR_LIVE, 2026-11-03)
- Added rr_post_temporal, live GUI control, off by default and in every preset. No ABI change.
- One render-res pass reprojects A/C/bloom with RR MV and jitter; alpha 1/16, current 3x3 clamp.
- Per-tap depth/MV rejection uses D24/fp16 precision and pixel footprint; failure selects fresh input.
- NGX reset/mode switch, skipped frames, toggles and render/output resize reset history.
- Final aligns AC/EB at uv + jitter/render size. Exposure stays fresh; no host-visible atomics.
- Six device-local fp32 images cost ~250 MiB at 2560 x 1067; GPU ms await the prepared live batch.
- CPU reference: constant/linear energy, alternating A mean 0.5 and 961x variance reduction;
  iid flipping variance falls 31x; disocclusion, NaN recovery, clamp and depth/MV checks pass.
- Build and all 133 CTests pass after fixing the redundant manifest entry; QA detectors pass.
- pp-diff main 5f17b553: 219 identical, 1 pp-same, 16 water differences from newer main.
- Byte identity HEAD d715c7f4: 236/236; rr_post vs main: identical. New entries: 2/2 valid.
- Manifest delta 0; standalone request +1 (2 entries), auto-discovered. No define multiplication.
- tools/live_batches/post_temporal: Dialogue3 and static S8, real Ultra, 0/1/0/1, separate K8/timing.
- K8 requires effective AC/EB; selective float traces retain AC/Z/EB for linear energy checks.
- Fake queue dryrun passes INVALID-with-gates and valid-with-gates-stripped. No jobs staged.
- Supplied hair residual 7.6/13.1 -> target 3.1; bright contours 7.3/6.1 -> target 1.5 (PQ x1000).
- Far-background float mean and stable face must stay within OFF-repeat noise in both ON windows.
- Check white hair over DoF and sun-lit rims in Test Dialogue3; quality and GPU cost remain pending.
