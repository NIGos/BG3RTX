## Strand holes at the zoom start: pool reuse (D) disproved, camera-step diagnostic (branch claude/hair-pool-reuse, READY_FOR_LIVE diagnostic, 2026-11-10)

- Disproved before this branch by lives: A (another build's TLAS traced: TLAS check 0 in every line, 164930/180301),
  C (full BLAS builds: 0 of 11 at the trip frames), B (row order: with rt_hair_row_stable=1 every first-seen line in
  180301 and 180949 shows 0 moved rows, holes remain: trips at G-buffer 2089-2095 and 2152-2158, down to 0 strand px).
- D (newcomer upload / free / eviction / pool growth overwrites or races an existing groom) disproved offline:
  - the first trip precedes every upload by 4 frames (2089 vs upload in build 2093; 2152 vs 2156; 2125 vs 2128);
  - same run 180301, static warm-up: 10 groom uploads (68 MB) while 420 of 460 G-buffer hair draws were strands,
    0 dropouts, 0 trips;
  - code: no eviction under the 512 MB groom budget; frees retire after 6 frames, GPU done + 2 and every live scene
    slot (bvhsplit.cpp:355, bvh_on_present); uploads copy into retired ranges between barriers (bvhsplit.cpp:3736,
    3767) and a LOD is used only after its recording completed; no pool growth or repack in any hole window.
- Common trigger in all four hole runs (125217, 164930, 180301, 180949): the first frame of the runner's wheel zoom;
  the arrivals are characters entering the lowered view. At that frame the build switches to its moving-camera mode
  (bvhsplit.cpp:2626/2813/3459). The rejecting stage under motion is not decided offline. No fix, no key.
- Added (CPU only, no shader, no ABI, variants delta 0): the frame watchdog line ends `build camera step X m at that
  frame, Y m the frame before` (hair_primary::CameraSteps). CTest hair_groom_asset_test test_camera_steps.
- hl1 job: wheel expect fixed (`phase=2`, derived from the plan; phase=1 made 180301 INVALID), expect_every on the
  camera step of every trip line in lower/watch; predicted.json first; lint 0 failing (live and frozen corpus).
- Next live predicted: trip lines in "lower" with a step >= 0.05 m, none in the static windows. Then the stage.
- Details: docs/RTX_HAIR_PRIMARY.md 5x.
