## Reflection-specific flicker preparation (branch wf8-reflection-flicker, PREPARED_OFFLINE, 2026-11-03)
- Reopened reflection attribution: wf2-wf7 whole-water rates were dominated by native glints and did not isolate reflections.
- Existing rt_debug 25 exposes signed relative compose delta and tier 1 / unresolved; no shader edit or DXC run.
- Selective rendered capture saves same-frame record coverage, native water and debug output; 8 frames per burst.
- Metric counts above-derived-threshold consecutive on/off events, including 1 -> unresolved and unresolved -> 1.
- Tiers 2-6 remain grouped. water_reflect tiers are a different calculation and cannot fill the missing subtypes.
- Native water-output counts are a proxy; SSR cannot be separated from glints/probes/normals without another observable.
- wf8: S8 Water Queens House, one visible_unfocused_fg launch, 24 phases / 185 s plus load, wf7 time-based turns.
- Reflection compose ON / OFF repeats / ON, RR-off attribution; static calibration at the matching central turn pose.
- All other water image terms OFF at startup and throughout compared windows; compose alone toggles, exposure held.
- Five 8-frame raw bursts, five 16-present beauty sequences, one mask trace: <=1,663,815,040 B under documented limits.
- Render pixel count is guarded at 3,686,400; display must be <=3840x2160 for the presented bound.
- Reflection-support masks score every beauty window; whole-water mask is secondary. No cross-turn pixel subtraction.
- predicted.json written before jobs: named outcomes for compose-tier, native-output and RR candidates, with limits.
- No stabilizer implemented: validated tier/radiance history needs a shader change; existing experimental keys stay off.
- GPU effect cost unchanged; capture-free ON/OFF windows collect existing compose GPU-ms. No live cost measured.
- Fake queue dryrun and scene-wait/capacity lints pass; metric unit tests are reported with the implementation.
- No game launched, no authorization requested, no commit or git metadata write. Shader request delta 0.
- Remaining: separately authorized live batch; unresolved-tier subtypes and isolated native SSR remain unavailable.

## wf8 live result (S8 Water Queens House, 2026-11-03 02:19, valid, every-window gates ok)
- Verdict: candidate 1 holds, our compose switches tier 1 <-> unresolved. Cause: not noise but a one-frame-old scene.
  In 95% of passes the compose traces the previous lighting's camera-relative TLAS (pick_scene kStash; "newer scene taken" 0-5 of 120) with this frame's camera, depth and radiance, and no shift (water_reflect has one, the compose does not). Under a turn the orbit camera translates, hits land beside the surface and fail depth_same_surface.
- Raw debug 25 tier-1 share of covered water: static calibration 25.3-25.9% (8 frames). Turn a: 0.4-0.9% on frames after a camera jump, 16.5% / 16.2% on the two frames where the camera held (same pose as a 0.4% frame). Turn b: 0.24-1.55% (all frames jumped).
- Window stats: static screen hit 33.0%, hidden 7.4%. All turn windows: hit 5.5-17.8%, hidden 15.3-28.5%. RR off is the same (hit 9.6-10.8%, hidden 15.7-16.3%): RR is not the source. Compose stability: 0 uncomposed frames, 0 on/off flips (the switch is per pixel, not per frame).
- Per-pixel toggle metric is confounded: the turn arrives in jumps of ~45-90 deg per rendered frame (presented frames and minimap spin a full turn in 16 presents), so adjacent-frame valid pairs are tiny (compose a 8/1730, b 2/3829; game proxy a 1564/3111, b 2919/409915) and the all-frame tier-1 mask is 0 px, so the presented-flicker score could not run. Test fix: smooth the turn input (smaller, per-frame PostMessage deltas) before the next run.
- Native water (candidate 2) is not decided by this run; RR (candidate 3) is weakened.
- Cost: compose GPU 0.10-0.15 ms median per window, unchanged.
- Fix (not a hysteresis): rebase the compose's scene rays like water_reflect's water_shift, o_build = o + (cam_now - cam_build) when the picked TLAS is older than the camera, behind a new key rt_water_compose_shift (off by default, spec constant).
- Captures 1.1 GB (under 2 GB): kept. Analysis JSON in .claude/worktrees/test-main/captures/wf8-analysis/.
