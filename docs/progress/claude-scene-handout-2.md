## Scene handout increment 2: scene TLAS by stamp address (branch claude/scene-handout-2, offline-verified, 2026-11-09)
- Layer: every scene commit writes the TLAS addresses into the slot's stamp (bytes 16 reference, 24 traversal = the OMM
  accelerated TLAS when the commit chose it); `rt_omm_scene_reference` downgrades 24 to the reference when a recording
  refuses the accelerated TLAS (never back up; `[rt] scene handout: N stamp traversal downgrade(s)` with the wait line).
- Shaders: constant 1051 (`kSpecIdSceneHandout`), `RT_TRACE_SCENE` in rt_common.hlsli at the 8 GI/visibility sites,
  ls_ref_body and hair_receiver: OMM variants trace word 24, others word 16, address 0 falls back to gScene.
- Core: live key `rt_scene_handout` (default 0) adds 1051 = 1 to gi/ and water pipelines; binding 18 stays the real TLAS.
- Key off: shader_identity vs main 84/226 byte-identical; the 142 that include rt_common differ in raw bytes only:
  tools/scene_handout_spec_check.py proves all 154 entries the same program with 1051 = 0 (others symbolic), and 1 differs
  in 142 (control). Identity run 242 s (148 compiles), equivalence 781 s. variants.txt delta 0. No ABI change.
- frame_replay `--handout 1|2`; self-test 4c: spec 0 vs 1 bit-equal on the synthetic pack, empty-TLAS control differs.
  No real replay pack on disk (lost at the 11-07 format). 222/222 CTests pass (retirement gate: 1051 fixed 0 like 1026).
- Live (not run): toggle rt_scene_handout 0/1 in one session; GI/shadow/RF energy and rt_primary_check equal, GPU ms equal.
