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
