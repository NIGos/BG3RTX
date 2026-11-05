## NR render-size feature retirement (branch dlss5-res, offline verified, 2026-11-05)

- Fixed immutable feature and recording-image keys: render/output sizes, guide
  extents/subrects/scales/convention, source formats, HDR and depth inversion.
  A changed key stops NR, polls existing submitted-work completion and waits for
  recording discard, releases the feature before its images, recreates and resets.
  No device-wide per-frame stall. Successful replacement logs both render sizes.
- Signed 310.8.0 Vulkan fixture: raw and layer-equivalent converted guide modes,
  reuse/recreate, 12 frames each: finite changed output, API success 0x1.
  Converted recreate also completed 48 frames / four creates. Crash not reproduced.
- Validation: only the known NGX-internal VUID 09600, once per feature
  (reuse 1, recreate 4); zero other errors. Exit 1 preserves validation failure.
  NR median 7.118 ms in the 48-frame diagnostic, not an in-game cost.
- CPU recreate test covers every immutable key field and ignores per-frame
  jitter/reset. Shader manifest delta 0. No git writes, game launch or queue use.
- `scripts/build.ps1 -Test`: 189/189 pass, build 21 s, CTest 266.03 s.
  Visual Studio environment was preloaded to avoid vcvars stderr aborting setup.
- Evidence: `.scratch/dlss5-res-*.log`; design/details in TRACK_DLSS5 section 7.
  Installed previous log read was denied. User-report chronology remains supplied evidence.
- In-game follow-up: after coordinator build/restart, Quality -> Balanced ->
  Performance -> Quality should log NR recreation, reset history and retain RR
  while old recordings retire. No in-game qualification was requested or run.
