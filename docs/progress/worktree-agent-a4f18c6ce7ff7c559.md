## M5.4c: the game's sun shadow on the water families that sample it (branch worktree-agent-a4f18c6ce7ff7c559, offline, WIP, 2026-11-01)

- Merged worktree-agent-a09041612a548766f (0994eff, water_audit pre-audit; CMakeLists conflict resolved: both test lists).
- Finding: PL_noCaustics_Shadows 06e66a6f (S3 camp pond) and huge_PL_Shadow ced49ee3 multiply their sun radiance by
  the game's visibility Vg = phi(1, max(0, pow(PCF of 7 Dref samples, 2.2))) (_1365 / _1869), then by two cloud
  layers and b0: every step from Vg to the colour is a product, so the sun term is D = Vg D_u exactly. Replacing Vg
  (not skipping) is what the FS allows.
- `tools/water_audit.py` `game_sun_shadow`: derives Vg and refuses unless every Dref sample reaches C only through
  Vg and Vg only through Lsun's two sun products. New FsIds field `vg`; both rows added (11 id tables).
- Patch (`common/water_fs_patch.h`, `gsh`): two words after the form's last, half2(Dg.rg), half2(Dg.b, Vg) with
  Dg = D (1 - Vg) / Vg = D_u - D (FS without Vg: Dg 0, Vg 1). Without the words a Vg FS stores lum(D) = 0: the sun
  step skips it (game shadow kept, never twice). gsh = false and vg = 0: modules unchanged (FNV pins pass).
- Compose (`WATER_GSH` variants of rt/water_compose.hlsl, 3 new requests): lit ray -> C + Dg, occluded -> R (as
  M5.4), unknown -> C; C' = V (C + Dg) + (1 - V) R. Stats words 44..53, log `[water] game shadow`, rt_debug 26 cyan.
- Key `rt_water_game_shadow=0` (restart to enable, needs rt_water_shadows), GUI "Water game shadow replaced",
  ComposeBuffer.flags (kBufGsh, from pad: no ABI change), stride = form + 8.
- Tests: water_compose_test 4k (FS model: V = Vg gives C within the halves' and target's rounding, Vg = 1 bit-identical,
  V = 0 gives R; 20k random samples x 4 encodings), the gsh patches of all 11 FS; water_audit_test spirv-vals the
  three gsh forms of every FS and checks the Vg refusals. Build + water tests passed (11-01).
- m5_promotion: third job `m5p3-camp-fg2` (Campsite - 27h, gsh at startup, follows rt_water_shadows), plan
  `m5p3_camp.json`; the gate adds the restored light to the prediction. Camp water is small in the 10-30 captures
  (~1% of the frame under the log bridge, 20261030-163004/-163010).
- CTest 82/84 in the build run; the 2 failures were the stale generated files, regenerated (`variants.txt` +3,
  `settings_schema.json`) and both tests rerun: pass.
- NOT done (paused): compile the 3 WATER_GSH variants (DXC) and check the old variants byte-identical; dryrun of m5_promotion
  with the third job; README/PLAN_M5_WATER sections; optional Debris (probe mask _774 = a factor of (Kd + Ks) k and
  the direct mix(1, mask, m1): an `amb` id would cover it; refraction sample is set 2 b4 as well).
