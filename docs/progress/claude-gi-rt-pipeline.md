## Ray tracing pipeline GI trace (branches claude/gi-rt-pipeline, claude/gi-rt-pipeline-ultra; on by default, 2026-11-08)
- What: the hit-GI trace (gi_trace_rt_hit_sun[_em].hlsl without SHaRC) runs as a ray generation shader over the
  same grid and descriptors (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"). ABI 29 (Bg3HostApi::rt_pipeline,
  3 VkDeviceFuncs entry points). Layer: VK_KHR_ray_tracing_pipeline + VK_EXT_ray_tracing_invocation_reorder when
  both are supported, dropped first on a device creation failure. Compute fallback for any missing piece, logged once.
- Shaders: +4 lib_6_6 requests (hit_sun, hit_sun_em, each with and without OMM); every other request byte-identical.
  Reorder is constant 1050 (gi_rt_pipeline_reorder; no prepass at 0).
- Offline seed gate on the shipped request (raygen vs compute, 32 draws per set, thresholds from compute vs compute):
  PASS on camp, inn and S8 (energy, per-pixel 4 sigma, counters).
- Live inn (The Blushing Mermaid, Ultra, FG): trace pass 12.09 / 11.88 ms OFF vs 10.03 / 9.86 ON, about -2.0 ms;
  the emission variant ran as a ray tracing pipeline, FG status 0x0, no device loss. Energy -0.32 / -0.35%
  (OFF-OFF spread 0.27%, one pair).
- Live camp (Test Campsite): clean window cost-on-b trace -2.12 ms (OFF spread 1.48); energy +0.13 / -0.04% (spread
  0.13%). Across both scenes the energy signs disagree, so no energy shift is shown.
- First raygen build about 60 s on a cold driver cache (compute meanwhile); instant once the driver caches it.
- Open caveat: one nvlddmkm event 153 at 17:18:05 in the camp's first ON window (cost-on-a). The GPU recovered and the
  game continued, with no fault line and no device loss. It is the only one in 42 queue runs over 30 s. Not
  explained. Check offline: frame_replay --ab-rgen with validation and GPU-assisted validation on the packs, and
  layer/devfault.cpp (VK_EXT_device_fault) output if it recurs.
- User decisions 2026-11-08: ship it ("in game mi sembra tutto ok, quindi se gira meglio teniamolo"), then on in
  every preset ("perchè non attivarlo sempre?").
- claude/gi-rt-pipeline-ultra:
  - exp_gi_rt_pipeline ("Faster bounce light tracing") is a qualified Performance control (no longer experimental or
    developer-only), preset {1,1,1,1}; code default gi_rt_pipeline=1 (bg3rtx.ini, kDefaults, core fallback). The
    release profile does not pin it, so release is 1. The exp_hidden_surface need is dropped: the key is a no-op
    without the hit-GI trace (core/gi.cpp hit_gi_dispatch is its only reader).
  - gi_rt_pipeline_reorder stays experimental, developer only, 0.
  - Tests updated with reasons: release control count 17 -> 18 (settings, web bridge, web bridge JSON); the reorder
    knob's prerequisite is now on in every preset (its unmet-need check uses gi_rt_pipeline off).
    CONTROL_CONTRACT.md has the new row.
  - No shader or request change (identity checked).
