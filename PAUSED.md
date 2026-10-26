# PAUSED: D-LS cost work (branch worktree-agent-a88047a9678afc2e5)

Paused on request on 2026-10-26, about 21:00. No game run was in progress (the validation
run was still waiting for the game lock and was cancelled). Nothing is merged.

## Done and verified
- **Profile of the old pass** (foundry, live phases). Registers: the old pass uses 252
  registers plus 16 B of local memory, so 8 warps per SM (`tools/pipeline_stats.py`, a new
  tool that prints the driver's register count with no game). Rays take about 0.40 ms of
  0.77. The rest of the cost per switch: `rt_origin` +0.065, `ao_direct` +0.07,
  `rt_bvh_split` +0.08 (confounded: the frame ran at 130 fps instead of 24), `ls_soft`
  +0.035, `mat_classes` 0 (its cost is in the registers, whatever the switch), the join
  0.011, `rt_alpha_test` +0.06. Full table in `tools/lsref/TRACK_D_9.9_draft.md`.
- **The optimised pass** (`shaders/gi/rt_local_shadows.hlsl`), bit-exact.
  - It has two variants: `LS_FULL=0` for the default settings, and `LS_FULL=1`
    (`rt_local_shadows_full.hlsl`, the old loop with all debug, stats and RIS modes).
    `core/gi.cpp` picks one per frame.
  - The default variant runs in three phases per 32 list entries:
    1. tests, with the BRDF evaluated only for traced lights;
    2. rays;
    3. reload the surface and compute the exact terms, with the sums in the old order.
  - Result: 96 registers, no local memory, 21 warps per SM.
  - The sun is not recomputed where both AO factors are 1, and the clouds are skipped
    where the sun mask is 0.
  - The specular-AA neighbour loads went from 4 to 2.
  - `gAux` is read only in `ao_direct=2`.
- **Join pass keeps unchanged slots** (`rt_local_shadows_match.hlsl`, `core/ls_lights.cpp`):
  - each slot caches its result, keyed by position and radius bits plus a records
    generation;
  - the slot image is now 4096x3 and the upload header 32 B;
  - cost 0.011 → 0.005-0.007 ms.
- **A/B tooling:**
  - `shaders/gi/ls_ref/ls_ref_body.hlsli` is the pass of f5e9eab, frozen by
    `tools/lsref/make_ls_ref.py`;
  - `ls_impl=0` runs that frozen pass;
  - `ls_check=1` runs old and new on the same pixels and logs `ls check`;
  - `ls_check=2` runs the old pass against itself;
  - the core now logs the join time separately (3 timestamps).
- **Image equality.** `ls_check=1` found 0 differing pixels in every window: foundry
  (also with `ls_owners=31`, `ao_direct=2`, `rt_origin=0`, `ls_soft=0`, `mat_classes=0`,
  `LS_FULL` with `ls_samples=1` and `ls_debug=2`), Grymforge (2.29e9 px) and Water Queens
  House (6.23e9 px). Kept join slots: 0 differ from a fresh search. `ls_check=2`: 0.
  Exceptions: in 3 runs, one `LS_FULL` window differed, each time in the window where the
  variant switched.

## Numbers (GPU timestamps, medians, same run, new / old)
| Scene | new | old |
|---|---|---|
| Foundry | 0.39-0.42 ms | 0.57-0.65 ms (−31..37%) |
| Foundry, no rays | 0.143 | 0.372 |
| Grymforge | 0.71-0.73 | 0.97-0.98 (−26%) |
| Water Queens House | 0.114 | 0.169 (−33%) |

The **0.35 ms target is not reached** (about 0.40 ms in the foundry). What remains is the
rays (≈0.25 ms) and full-resolution G-buffer traffic (≈0.14 ms).

Tried and dropped, all measured:
- rays ordered by wave union: +0.007 ms;
- a separate trace dispatch at 72 registers: +0.07 ms;
- no ray for lights of colour 0: no change;
- relative-threshold skip, not exact: no gain, and 125 registers.

The GPU was shared with the user's desktop and games, so absolute values drift by about
±10% between runs.

## In progress, not done
- `-Validation` run with the new code. The baseline was captured on f5e9eab (8 IDs:
  10788, 09600, StorageImage-FormatMismatch, OutputNotWritten,
  non-acquired-swapchain-image-used, vkDestroyImage-01000, CopyBuffer-00118, 08737).
  The new run was cancelled before it started.
- `-Move` run, and the non-finite check on its dumps.
- Docs: `tools/lsref/TRACK_D_9.9_draft.md` is the draft of TRACK_D_RT.md §9.9; its
  `{VALIDATION}` placeholder is still open. Not yet in TRACK_D_RT.md (also update §9.5,
  the "base has risen" note in §9.8, and the bg3rtx.ini comment "About 0.35 ms"), and
  the QUALITY.md row is not written.

## Next steps
1. Rebuild: `scripts\build.ps1`. Copy `data\assetdb` from the main checkout if it is
   missing.
2. Validation: `tools\lsref\run_phases.ps1` expects to live in `captures\`. Copy
   `tools\lsref\{run_phases.ps1,run_all.ps1,ls_val.json,ls_scene.json}` to `captures\`.
   Then run `.\captures\run_phases.ps1 -Phases captures\ls_val.json -Tag val -Validation`
   and compare the IDs of `captures\validation.log` with the 8 baseline IDs above.
3. Move: `.\scripts\auto-run.ps1 -Background -NoBuild -Move -Traces 2`. Check that every
   `nonfinite` in each capture's `dumps.json` is 0.
4. Put the draft into TRACK_D_RT.md §9.9 and update §9.5, §9.8, QUALITY.md (a status
   row, and R7 noting Grymforge at 0.72 ms is over budget) and the bg3rtx.ini comment.
5. Decide whether to keep the A/B tooling (`ls_ref`, check shaders) in the tree. It is
   useful for later work, but `make_ls_ref.py` must be re-run from the new HEAD so that
   the next change is checked against this pass.
6. Squash or reword the WIP commit, then commit with the Co-Authored-By line.
