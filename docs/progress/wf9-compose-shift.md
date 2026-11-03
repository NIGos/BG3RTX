## Water compose camera correction (branch wf9-compose-shift, READY_FOR_LIVE, 2026-11-03)
- Defect: S8 water reflections switch on/off while turning. Cause in our compose: it traces a one-frame-old
  camera-relative TLAS, so screen hits fail the same-surface depth test (water_compose.hlsl:442).
- Key rt_water_compose_shift (SpecId 1015, off in every preset, Experimental GUI "Water camera correction").
- First version shifted by the previous copy's camera: no effect. wf9 live with smooth Q/E turns (11:56):
  compose screen hits 6.4-7.9% vs water_reflect 18.6-19.8% in the same windows, hidden 24-28% vs 12-14%,
  shift 0 and 1 identical; the one frame with a same-frame TLAS recovered (14.3%). Energy and cost neutral
  (compose 0.10-0.15 ms).
- Fix now: host-written 4-entry GPU ring of TLAS build cameras; the compose shifts by the camera of the TLAS
  it actually traces (also for F-1 hit shading). Missing/stale cameras refuse the shift. No hysteresis.
- Compose stats now log TLAS age and build-camera delta. Unit tests cover the ring; watershift spec check
  15/15 (key-off 30/30, reference 60/60); pp-diff main 219 identical + 16 changed; ABI unchanged.
- Prediction: turning screen hits ~19%, hidden ~13%, static energy unchanged. Needs the wf9 live (layer restart).
- In game: S8 Water Queens House, turn with Q/E and watch the pool column reflections stay on.
- Full history: docs/archive/wf9-compose-shift-history.md.

## wf9 exact-camera live (S8, 2026-11-03 13:10-13:16, VALID 23/23; NOT FIXED, shift 1 regresses)
- Turning (cost/snap/diag/return): water_reflect hit 18.8-19.6%, hidden 12.4-14.1%. Compose shift 0: hit 6.4-7.3%, hidden 25-26% (as at 11:56). Shift 1: hit 0.0-1.8%, hidden 0.1-3.0%, miss 92-97%: the traced reflection is gone.
- Static shift 1 too: compose hit 15.0% -> 0.0-0.8%, miss 76% -> 99%. TLAS age 0.91-1.00 avg, max 1 (2 once), camera missing 0.
- Build-camera delta: static 294.80 m (must be 0), turning avg 303 m, max 312 m: the two cameras are in different spaces.
- Cause: the ring stores the 224 B scene camera (cam2) float4 13 (layer/rtscene.cpp:315), the compose subtracts it from CameraCB float4 13 (cam.x[3], shaders/rt/water_compose.hlsl:161 and 344). water_reflect uses cam2 v[13] on both sides (water_reflect.hlsl:131).
- Fix: subtract the current frame's cam2 float4 13 (copy in.cam2 +208, 16 B next to the ring entry, core/water.cpp:1265), and add a static gate: delta 0 at an unmoved camera.
- Raw debug 25 tier 1: static 15.1%; turning shift 0 4.6% (13373 toggles, rate 7.2%), shift 1 0.0% (99101, 99103; 0 toggles, nothing left to toggle). Flips 0, except 1 at the first switch (8 uncomposed).
- Energy (water-compose): OFF 0.187590 / 0.187820, ON 0.187536 / 0.187781, within noise, but ON drops the compose delta (-0.0013 to 0.0000 per owned). Compose GPU: OFF 0.104 static / 0.136-0.147 turning, ON 0.085-0.090 (nothing traced on screen).
