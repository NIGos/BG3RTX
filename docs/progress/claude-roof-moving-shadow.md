## Stable roof selection rebased onto main (branch claude/roof-moving-shadow, READY_FOR_LIVE, not merged, 2026-11-15)
- From fix-roof-moving-shadow (031d683e, 2026-11-03). Defect: key off, core/sun_trust.cpp re-reads the roof volumes
  around the delayed camera every 30 frames or 5 m, with a radius read back from the screen and against unpadded
  bounds; shaders/gi/sun_roof.hlsli tests padded boxes on an infinite ray. The uploaded roof set can change while a
  receiver and its sun ray stay still: a sun shadow that moves under a roof.
- Key `rt_sun_roof_world` (0 by default, live; knob `exp_world_roofs` "Stable roof selection", Developer, off in every
  preset): the whole level's roofs from a fixed origin, once per level or padding change
  (common/sun_roof_selection.h). `sun stats` then prints `roof query -1 m`.
- Fixed with the key off too: a cleared roof list is restored in the same frame (it could stay empty until the next
  periodic refresh), and live pad / up changes refresh at once. Key-off queries keep the 30-frame / 5 m policy.
- Rebase: knob rewritten in today's table format as a developer knob (the release profile has no experimental user
  controls); settings_test checks added next to main's; defaults regenerated (already up to date); schema rewritten
  (+13 lines); `rt_sun_roof_world` registered in pipeline_readiness NO_PIPELINE. No stats counter, ABI or shader change.
- Gates: shader_identity against 67627237: 225 of 225 requests byte-identical (identical inputs); variants.txt
  request delta 0. sun_roof_selection_test 1216 checks, settings_test 34404 checks, assetdb_test (CTY: 398 volumes,
  records identical across a camera cut). build.ps1 -Test: see the commit message.
- Cost: CPU only (one roof query per level instead of one per 30 frames). GPU: the roof loop of rt_sun_resolve runs
  over all uploaded volumes (398 at CTY) instead of the local subset: predicted +0.00 to +0.10 ms, not measured.
- Live (staged, not queued): tools/live_batches_staged/roof_shadow rs1-dialogue3-roof-world, Test Dialogue3, key
  0 / 1 / 0 in one run, about 4.3 min after the load, visible_unfocused_fg. predicted.json first; live_flow live and
  frozen 0 failing; lint snapshot append only.
- The live must show: ON `sun stats` lines with `roof query -1 m` and the whole roof count; the band over the still
  left character stops in ON and returns in both OFF windows (4 PNGs per state); energy of ON inside the OFF pair
  (floor 0.5%); rt passes GPU ms within noise + max(0.1 ms, 1%). A band that stays with a fixed roof set rejects the
  key as the fix (the native mask kept under roofs in mode 0 is then the cause). Not a quality pass until then.
- User check after a go: Test Dialogue3, Developer page, "Stable roof selection" On; watch the left character's
  face and chest in the sun for the moving dark band.
