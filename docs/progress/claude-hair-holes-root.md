## Strand holes under the wheel zoom (branch claude/hair-holes-root, READY_FOR_LIVE, 2026-11-10/11)

- 6c, cause 1 (fixed by the existing key `rt_hair_header_cam_gpu`, in hl1): the header camera was a CPU read of the
  camera UBO at the build's recording, the groom points are converted on the GPU; under motion the read can be one step
  behind and every hit is one step off its card. hc1 234025: GPU copy -> origin 0 in 18 of 18 samples; CPU -> 1-12 cm.
- Live hl1 000345 (key on): origin 0, zoom coverage 50-70% (was 2-7%), still 7 trips; the loss follows the camera step
  (1.31 m: 5%), all or nothing per draw. The 0-trip prediction was wrong.
- 6d: build stamp (layer only). Live hl1 003637: all 5 trips (2448-2452) "of the applied publish's build", 0 newer in
  every period: the "header one publish older than the BLASes" candidate is DISPROVED.
- 6e: BLAS refit ruled out from runs on disk (the loss is gone when still, refits only). No rebuild key: grooms are
  compacted, a full build needs the uncompacted size. Lost first: the far LOD 2 grooms (58da 64/0, 6a5d 74/0).
- 6f, offline read of the three candidates left (no fix found):
  - view-projection: the G-buffer side uses one block (gCam.vp) for card point, ray, depth and band; the points are the
    exact inverse of the prepass block's VP, so a jitter or projection difference moves no point. Open only: the block
    rewritten between the prepass draws and the conversion, or a VP that is not view x projection (both in the sampler).
  - entry reuse by a second G-buffer of one label: ruled out for 003637 (labels 2448-2452 each found their own build
    stamp, builds logged once per label). 000345 has a label gap at its long frame: open for that frame only.
  - game LOD switch: in all 8 hl1 logs a new hair draw arrives at the first trip frame or the one before; unmatched
    (not swapped), rows and BLASes unchanged, the loss lasts 5-7 frames and follows the step: zoom start coincidence.
- 6f, dense sampler built: key `rt_hair_dense_sample` (developer, off, pinned 0 in release.ini), specialization
  constant 1052 (`kSpecIdHairDenseSample`) of the strand G-buffer variants, added only when on. A quarter of the
  card-marked fragments store a 96-byte sample, 4 per draw slot (64 a frame), behind the pixel record (device-local);
  the next build copies 6 KB behind the counts readback. Per sample: reason (accepted, miss, other draw's strand,
  segment, card band, UVs, tips), reused or private, ray origin and direction, card and hit distance, band,
  strand point off its ray, sine against the block's view x projection, camera, rows, the groom's anchor point.
  Layer: camera step from the GPU cameras, anchor drift in the world per key, LOD and key age; lines
  `[hair primary] dense sample:` (moving and watchdog frames) and `[hair primary] dense samples (last N frames ...)`
  (still | moving). CTest hair_groom_asset_test test_dense_sample.
- Signatures (predicted.json dense_sampler_6f): displaced points -> anchor moves cm or more per frame while moving;
  inconsistent camera block -> sine over 0.1 mrad; entry reuse -> reused entries with the strand point cm off its ray;
  game LOD -> rejects only on new keys or cards metres from their anchor; other publish's key map -> "strand of another
  draw"; trace or BLAS/TLAS -> misses with the anchor still and the ray right.
- Identity vs origin/main: only rt/hair_primary_gbuffer.hlsl changed (the constant is in the module; 0 runs none of
  it); variants.txt delta 0. Cost on: a 4x4 inverse and a 96-byte store per sampled fragment (read the strand G-buffer
  GPU line against 0.206 ms). hl1 regenerated: rt_hair_dense_sample=1, pipeline line with 1052 on, the period line with
  sampled frames in lower and watch, sample lines in lower; trips reported, not gated. Lint 0 failing (live, frozen).
- Next live shows which signature the moving samples carry; the fix follows from it. User check once fixed: S8, wheel
  zoom in fast: the hair keeps its strands, no pop to cards.
- Left: lint_snapshot on this PC shrinks the checked-in snapshot (needs a merge mode); the CPU camera also feeds the
  cascade caster rebase (rt_cascade_origin=0), the groom LOD bounds and rt_camera_moving.
