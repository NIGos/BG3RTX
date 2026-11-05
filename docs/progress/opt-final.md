## Optimisation night close-out (branch opt-final, complete, 2026-11-05)

- Removed rejected cpu_opt4 code, resource/descriptor receipt tracking, key,
  NO_PIPELINE entry, opt_r5 job and three candidate tests. Retained frozen lint
  receipts. Evidence remains in TRACK_D_SCENE_BUILD and the historical CPU4 note:
  inn Ultra hook bodies +0.47 / +0.54 ms/frame vs OFF spread 0.104 ms.
- Retained qualified cpu_opt1/2/3 and rt_scene_compact_holds, all default 1;
  aligned cpu_opt1's hot-settings fallback with its canonical default.
- OPTIMIZATION_20261105 now opens with every night candidate's outcome and the
  tooling fixes: quiet readiness, layer-only keys, shared A/B analysis, prewarm.
- Inn Ultra CPU receipts total about -0.67 ms/frame (12.4 -> about 11.7);
  High inn scene build -0.87 ms GPU, energy within OFF spread. Other scenes
  were not A/B measured per candidate; no combined-preset measurement claimed.
- Check in game: inn save with the qualified defaults; developer A/B keys remain.
  No new game launch or queue staging. No commit. ABI unchanged, shader requests +0.
- Validation: scripts/build.ps1 -Test built successfully; 181/183 passed
  (283.05 s). Fixed the CPU4 fragment length; shared-cache SQLite write failed
  under sandbox restrictions. Copied the index and water blobs read only into
  .scratch/shader-cache; BG3RTX_SHADER_CACHE selects that writable local cache.
- Final incremental build passed (3 s); both failed tests plus CPU1/2/3 and
  settings checks passed 9/9 (1.83 s). All 183 suite tests passed across runs.
  git diff --check and progress-fragment validation pass; no changed/untracked
  file exceeds 20 MB. No files staged.
