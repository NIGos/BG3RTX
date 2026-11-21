## gi_far_roof_emission offline A/B on the inn emission pack: NOT RUN, the replay lost the device (branch claude/em-pack-result, 2026-11-21)

- Pack: test-main captures/replay-20261121-071523-1 (1579 MB, pass gi_hit_sun_em, frame 2077, casters 1222447 tris,
  emissive scene 256 texture / 16 cache elements). Kept: it is still the input of the A/B.
- The first step (frame_replay --pack <pack> --spec 1054=0, test-main d4ee3e0a binaries) ended in `wait result=-4`
  (VK_ERROR_DEVICE_LOST) before any output. Not rerun (the GPU crash rule). No verdict, no "vs live" line.
- Cause: the pack's own specs carry 1051=1 (rt_scene_handout on in the release profile): every scene trace takes the
  TLAS address in the stamp s1b9 bytes 16/24, which holds the live game's address (0x2073200000). The gi_hit_direct
  packs never carried 1051, so this was new with the full-request contract. Validation also named s1 b19
  (gNativeTilesetSampler, 16 samplers), missing from kGiHitSunEm.
- Fix (this branch): frame_replay stamps the replay's TLAS whenever the effective 1051 is nonzero and --handout is not
  given (note "scene handout (constant 1051) on ..."); kGiHitSunEm gains s1 b19 (new packs capture it, this pack binds
  the default sampler). replay_pack_test, frame_replay_test, pt_reference_test, frame_replay_ls/rf_test pass.
- Next: after the merge, the offline steps of r2_tools/em_pack/predicted.json on the same pack. The rerun is the
  coordinator's / user's call (it is the command that lost the device, with its cause removed). Key stays 0.
