## Compose camera correction (branch wf9-compose-shift, READY_FOR_LIVE, 2026-11-03)
- wf8 S8: about 95% of compose passes used the older camera-relative stash TLAS without rebasing rays.
- Added rt_water_compose_shift, default 0 in INI and every preset; Experimental GUI Water camera correction.
- SpecId 1015; no ABI change. shaders/variants.txt request delta 0.
- water_copy preserves outgoing CameraCB float4 13 in cCam bytes 400-415 before overwrite.
- Validity rejects the first copy; finite/range checks match water_reflect. CPU has no stash camera.
- Earlier stash and stash-kept picks set the shift flag; same-frame, newer and refused picks do not.
- Reflection, sun, local-light and refraction origins shift by cam_now - cam_build.
- Distances are translation invariant; hits, light vectors and screen tests remain in current space.
- Compose binds the already-declared camera buffer; glint and compose keep matching pipeline suffixes.
- CPU unit tests cover all 12 scene-use/pick cases and disjoint flags; frame pacing tests cover stalls.
- Build and all 118 CTests passed; final GUI dependency/schema update passed all 4 settings/launcher checks.
- spec_cascade_check --key watershift --all --baseline main: 14/14 requests, 56/56 comparisons pass.
- Both values match reference branches; frozen key-off matches main for all 28 compose/copy entries.
- pp-diff main: 231 requests, 209 identical inputs, 22 changed preprocessing; no failed preprocessing.
- Raw modules contain the new constant; key-off preservation is proved after specialization, not raw byte identity.
- Unqueued S8 batch: 23 phases / 281 s; static and turning 0/1/0/1 within one launch.
- Frame-paced turns post 1 pixel per 4 observed rendered frames, never wall-clock catch-up deltas.
- Four raw bursts, 80 presents numbered p00-p79 across five folders, four static energy traces; bound 1.915 GB.
- Fake dryrun and scene-wait/capacity lints pass. Initial 6 min allowance failed; corrected to 7 min including load.
- Prediction: ON turning hidden share about 7-10%, covered-water tier 1 near 25%; static float energy within OFF noise.
- GPU delta unmeasured; wf8 compose was 0.10-0.15 ms. Every cost window requires compose GPU ms and stability stats.
- In game: S8 Water Queens House pool, look for stable column reflections during smooth turns; quality gate pending.
- No game launched, no real job queued, no commit or git metadata write. git diff --check passes.

## wf9 live result (S8 Water Queens House, 2026-11-03 03:48-03:54, INVALID on a gate, data read; NOT FIXED)
- Turns were still not smooth. Each 1 px frame-drag post (every 4 rendered frames) gives one ~45-90 deg camera jump, then the camera holds (presents: 1-2 jumps per 16, mean abs diff ~20 vs 0.4-2).
- Raw debug 25, covered-water tier 1 (static 99100: 14.7-15.4%): the jump frame drops to 0.0-1.2% with shift 1 (99101 f5857, 99103 f8968/f8971) and with shift 0 (99102 f7409/f7413). Held frames: shift 0 12-23%, shift 1 18-26%.
- Compose stats, hidden (screen hit): static 6.4-6.5% (14.9%) for 0 and 1. Turn cost windows: shift 0 16.7/17.5/18.0/17.1%, shift 1 17.4/17.8/17.7/17.6% (hit 14.3-15.4% for both). Predicted ON 7-10%: not met.
- Stability: every window composed 120/120, 0 uncomposed, 0 flips. water_reflect hidden also goes from 5.9% to 11.8-14.6% in turns: the turn poses differ (rays/frame 233k -> 400-457k).
- Energy (water-compose float mean): OFF 0.187359 / 0.187538, ON 0.187707 / 0.187474. The scene-only mean before the water varies by 0.0004 between repeats: unchanged within noise.
- Compose GPU median: static OFF 0.104, ON 0.099 ms. Turn OFF 0.127-0.131, ON 0.115-0.129 ms. No added cost.
- Verdict: the shift does not remove the on/off. Tier 1 is lost on the camera-jump frame with both values, so the stale-TLAS translation is not the deciding cause under this input. The fix stays off and unqualified.
- Next test: the camera must move a little on every rendered frame. The input posts 1 px but the game turns the camera in big steps.
- INVALID cause: the gate counts a stats line only if the line before it came after the settings read. scene-wait/casters lines come every ~4.0 s (120 frames at 30 fps). In energy-static-3, lines at 52.976 (7 ms before the 52.983 read) and 56.975 (straddles), then the 59.16 trace stalled the next one to 03:50:03.270, after the window ended at 03.008.
- Job fix: in plans/wf9_s8.json, energy-static-0..3 change "seconds": 10, "motion_start": 10, "motion_end": 10 to 14 / 14 / 14 (2 periods 8 s + 2.3 s trace stall = 10.3 s worst case). In the job file, "estimated_phase_seconds": 281 -> 297.

## wf9 Q/E live result (S8, 2026-11-03 11:56-12:03, valid 23/23 phases; NOT FIXED)
- Turns were smooth: every present pair differs (mean abs 7.8-12.8 vs 0.1 static, no jumps), raw game proxy 0.024-0.052 per frame, about 4 deg per rendered frame.
- Pose confound: the turns face the sea (covered water 600k px vs 275k pool), so the reference is water_reflect in the same windows, not the static view.
- Same turn windows: water_reflect hit 18.6-19.8%, hidden 12.2-14.4%; compose shift 0 hit 6.4-7.6%, hidden 23.8-27.5%; shift 1 hit 6.6-7.9%, hidden 23.9-27.7%. Miss (49.5%) and off screen (17.4-18.6%) match water_reflect: the TLAS trace is right, the screen-hit test fails.
- Static: compose hit 15.0%, hidden 6.4% for both values (water_reflect 14.8 / 5.9). Flips 0, except 1 at the first shift switch (pipeline rebuild).
- Raw debug 25, tier 1 of covered water: static 12.1-12.9%. Turning shift 1: 1.3-4.5% (99101, 99103). Shift 0: 2.3-7.1%, but 14.3% on f7720, the only frame whose TLAS is from the same frame ("newer scene taken" 3-6 per 120).
- Energy (water-compose): OFF 0.187525 / 0.187869, ON 0.187837 / 0.187675. Compose delta -0.000116 to -0.000132 in all four. Unchanged within noise.
- Compose GPU median: static OFF 0.104, ON 0.099 ms; turning 0.131-0.146 ms for both. No added cost.
- Verdict: the one-frame-old TLAS decides the flip (same-frame TLAS recovers), but the shift has no effect, so its build camera is wrong. The shader uses the previous copy's camera (water_compose.hlsl:159, stored at 321), not the camera the TLAS was built for. water_reflect gets its camera from the host for its build (core/water.cpp:1424).
- Fix: shift by the TLAS's build frame. Keep a 4-entry ring of copy cameras on the GPU, indexed by frame. Pick the entry for the stash's rt_scene built_frame (core/water.cpp:1202/1228). No ABI change. Log the TLAS age in stats.
- Fallback if hits stay hidden: rt_water_hit_shade for tiers 2/3 (water_compose.hlsl:455). No hysteresis (it lags and is not physically correct).
