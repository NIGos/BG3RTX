## M5 water pre-audit of every dumped FLUID pair; Flymm's family mapped (branch worktree-agent-a09041612a548766f, offline, READY_FOR_LIVE, 2026-11-01)

- Merged `m5-promo-tools` (a88947e). New `tools/water_audit.py`: every Fluids forward pair of the asset DB with
  its dumps; VS contract (gl_Position = vec4(P, 1) * set 0 b2 _m2, y negated, height-only displacement) on the
  spirv-cross GLSL, and the FsIds derived from the raw-id disassembly by the compose model's end of main (mix with
  Refr / a, fog, Lw = direct * mix(1, 1, m) + (Kd + Ks) k, probe and DFG terms, sun Fresnel, three light loops,
  channel, z). It reproduces the three hand-made tables id for id.
- Supported pairs 9 -> 16 (`common/water_policy.h`): + huge_PL_Shadow, flowmap_Brine, huge_PL_SphereMask,
  PL_noCaustics instanced and static, flowmap_OnlyNormals, Debris. Compose id tables 3 -> 9
  (`common/water_fs_patch.h`): + flowmap_PixelLighting, huge_PL_V2, flowmap_Brine, huge_PL_SphereMask,
  PL_noCaustics (both). All 9 patched dumps pass spirv-val in the four record forms (M5.3, sun, local, refraction).
- Not mapped (supported, keep the probe): 06e66a6f PL_noCaustics_Shadows and ced49ee3 huge_PL_Shadow (the sun term
  carries the game's cascade shadow, 7 Dref samples: the M5.4 sun step would shadow twice; needs a sun-step exclusion
  first), Debris (a mask scales the probe term), 1d4bff2e, 857ccb90, 68068cab, 4af3450e (no light loops). Rejected:
  WIP (VS displaces x, y, z). No dump: GlowLichen, flowmap_Shadows, flowmap_Test, Minimap, Reflections instanced
  pairs and 20 static variants.
- Flymm Cargos Basement: level CTY_DockWarehouseBasement_A (planes NAT_RVR_DockWarehouseBasement_Dock_NoSub,
  _Grates) = FLUID_Water_Plane_flowmap_PixelLighting_noCaustics, now mapped in both variants. The m5p2 run is
  expected valid; its `[water] families` line confirms.
- Batch m5_promotion: both jobs `dump_shaders=1` (only modules without a file are written) so one run dumps any
  missing water pair; dry run passes. After it: `py -3 -m tools.water_audit --dump <test tree>\captures\shaders --emit`.
- Tests: water_policy_test (families 9..15, WIP / BloodRiver unknown, unique pairs), water_compose_test (every table
  FS patched; the FNV pins stay on the three earlier FS), new CTest water_audit_test (tables = derivation, refusals,
  spirv-val of every patched module). Keys off: no shader file changed, so the ordinary SPIR-V is byte-identical.
  Cost: none until the keys are on (the same per-pixel stores as the three mapped FS).
