## Native DLSS5 fixture gaps (branch dlss5, PARTIAL / externally blocked, 2026-11-05)

- NR signatures are log-only, without an unsigned option. Product order: game folder,
  then user path; core stays verified. No NVIDIA binary is modified or bundled.
- Corrected motion to DCS RG32F. Cached staging and CPU vectors replace mapped-memory
  scanning. Feature, images, pipelines, descriptors and UBOs persist per run.
- Added frame CSV: GPU NR/encode/decode, evaluate CPU, submit/wait, preparation,
  NVML clocks/P-state/utilization and memory budget. Median/p90 CPU tests pass.
- Signed 310.8.0, 3840 x 1600, 72 frames / 12 warmup / 60 measured: NR median
  6.519856 / p90 7.146592 ms; encode .119776 / .174080, decode .102144 / .104160.
  NR maximum 7.539744 ms; finite changed output; validation off.
- Competing Cyberpunk exited before final run. Exact half-decoder lookup removes
  millions of ldexp calls between submits; prior idle run max 65.918176 ms, final
  max 7.539744 ms. Historical ~97 ms cause remains unknown. Evaluate CPU median
  .327800 ms; usage 1.63 GB / budget 32.95 GB; SM 930-1950 MHz, MEM 7001 MHz, P3.
- Custom 310.8.3 HashMismatch is app-accepted; core rejects its signature:
  Available 0, FeatureInitResult 0xbad00004, Create 0xbad0000b. No NR median/p90 exists.
- Validation blocked: fixture handles 0x30..0x42 versus internal 0xc5 GENERAL/UNDEFINED,
  VUID 09600. General/read-only/RG32 tests do not fix it. No filtering or SDK patch.
- ReleaseFeature 0x1 + device idle 0 still precede Shutdown1 stall > 30 s. Added
  explicit no-op/process-exit retention; successful sequence exits 0. This is not
  clean SDK shutdown. --try-shutdown preserves the bounded repro; no unsafe unload.
- Evidence: .scratch/dlss5-signed-cached72.log, core-vulkan-28508/timings.csv,
  dlss5-custom-final.log and dlss5-validation-final.log (both exit 1).
- No game, queue, git writes, production shaders, presets or ABI changes. Native 2b
  needs no D3D12 or split. NOT READY_FOR_LIVE: validation/custom-core blockers.
