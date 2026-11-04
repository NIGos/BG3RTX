# BG3RTX progress - 2026-11-05 (consolidation)

## Report: project consolidated offline, final qualification ready

**Final qualification live (user go, 2026-11-04 23:24-23:38, main 66ed523b, release candidate rc-20261104-66ed523b PASS 12/12).**
- Stock hair, Dialogue1: VALID, 123 s. Float energy +0.23% vs 21:04 (0.37525 vs 0.37439). One-sample costs vs rel3:
  RT +0.57 ms, GI +0.45 ms, GI trace +1.0 ms (9.84 ms): single windows, not a measured regression.
- Ultra, Water Queens House: every Ultra window gate passed in two launches (statics, pans, rotations, Ctrl+Shift+F9
  A/B); 0 device lost, 0 deferred destroys. GI 11.3-12.2 ms in measured windows (11.46 ms at 17:52), RR 2.38-2.40 ms,
  post temporal 0.14 ms. First launch timed out on the cold shader cache (50 compiles after today's shader changes).
- High part not completed: its readiness waits were generated wrong (rt_build=auto printed as 1; a box-light
  pipeline that High never builds). Test-job defects, not product faults; the alpha/OMM path High uses is the same
  as Ultra's. No further reruns (lean rule). Post-release: fix release_qual/make_final.py readiness for presets.
- Found by the user before the live and fixed: the package lacked the runtime alpha table (all foliage was opaque in
  ray tracing, "cardboard" tree shadows); release logging was verbose (now quiet, Detailed logging switch).

**Only question left: approve release candidate rc-20261104-66ed523b for upload?** Files:
`.scratch/rc-test/rc-20261104-66ed523b-d3d675cc/` (Setup.zip, Setup.exe, portable ZIP). Nothing is uploaded by agents.

**Done (all on main, pushed).**
- Five independent reviews (layer; core, shaders, common; product surface; shaders; tools). Every finding
  confirmed in the code first; table with the outcome of every row: [CONSOLIDATION_20261105](CONSOLIDATION_20261105.md).
- Fixed: 19 layer findings (resource lifetime kept until GPU completion, per-queue pruning, FG state only on
  SDK success, init/bind/recording failures unwound, asset DB bounds, latches), 8 core, 15 product (launcher
  failure paths, rejected options out of the opt-in, ZIP install rollback), 5 shader (rr_post overflow,
  alpha mip clamp, UV edge loads, reprojection validity, half-float clamps), 20 tooling (shader identity trusts
  only intact blobs, QA gates fail on missing data, deterministic asset DB, about 170 unregistered tests).
- Not confirmed, written with the reason: C4, C7, C8, C12 (rings larger than frames in flight).
  Deferred with the reason: L11, L12, L17 (need submission-dependency tracking), S-F05, S-F02, T2.
- Cleanup: 8 of 9 rejected options removed (pak publication, generated rows, IP grass, alpha spill, water
  hit shading and cost bits, half resolution, current sun, GI guards, 3 GPU candidates). SHaRC stays: its
  device capability also serves water and hair. Shader requests 167 -> 122; about 4,000 source lines removed.
  Release path proven equivalent with frozen specialization constants (71 identical, 139 equivalent, hair
  requests identical against main, control differs). Last code with them: tag `archive-20261105/pre-cleanup`.
- UI/UX (Opus review, 26 changes, before/after screenshots at 100-200%): plain help texts with restart notes,
  Experimental shows only Water surface reflections (strand hair hidden until its update, user decision),
  preset review, Help & support, first-run folder notice, installer texts.
- Installer: Inno Setup 6.7.3 pinned by hash, same payload as the ZIP; status text while preparing;
  lifecycle install 63 -> 15 s. Payload 229 -> 219 files (removed shaders).
- Release check PASS 12/12 on the candidate ([RC_CHECK](release/RC_CHECK.md)): build, 219 files, hashes,
  licences, ZIP install/update/uninstall, setup install/update/uninstall and Apps entry.
- Tests: 172+ CTests green at every merge; merge-green now lints only active or new live jobs.
- Nexus kit (`docs/release/nexus/`): title, short and main description (the published version plus intro,
  Beware of Dragons, NVIDIA RTX + Vulkan only), sticky post, header 1300 x 372, graphics script.
- Lives today (user go): rel1 Ultra S8 VALID 166 s, rel3 stock hair VALID 127 s on main ce1448bf
  (after the layer fixes): no deferred destroys, no device lost, RR temporal 0.12-0.13 ms.

**Open, not claimed.** Live confirmation of the cleanup, shader fixes and UI on the release build (the final
qualification); S8 float energy has no historical baseline (reported as unavailable, not pass); signing;
fresh-PC install; Act 2/3 acceptance (user).

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).
