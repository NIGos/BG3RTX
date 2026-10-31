# Pool lag batch (live_batch pool-lag), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the user must be away), FG 2, keys toggled inside the run, diagnostics only in `diag-*` windows. One job, no PNG traces.

Question: are cached static (pool) instances placed away from the raster while the camera orbits? (Inn evidence: rotation pool in front 17-19%, soup 1.3-3.8%; static pool in front 0.07%.) The code has no camera-origin term for the pool: the TLAS matrix is the game's own camera-relative instance row, copied at vkQueueSubmit, and the BLAS is object space (docs/INN_ROTATION_MATCH.md, "Pool lag check"). This run measures it directly.

1. `pl1-inn-pool-lag-fg2` (The Blushing Mermaid - 96h 39m, 100 s): warm-up; `diag-static-a` 15 s; `diag-turn-q`, `diag-turn-e`, `diag-turn-q2` 15 s each; `diag-static-b` 15 s at the view the rotation left behind; restore. Diag windows: rt_stats, rt_primary_check, rt_primary_epoch, and mesh audits raised to 8 per frame, each mesh every 120 frames (restored to 2 / 600). Startup: `rt_primary_fade_class=1` (diagnostics only; loads the camera-fade table), `rt_timing_frames=120`.

## Gates (read per window)
- Pool displacement: `[bvh] audits while the camera moved: N ok, M failed, max error E x tolerance`. Each audit compares a cached mesh's TLAS matrix (read at submit) with the same frame's raster capture of that draw (tolerance 2 mm + 4e-5 x distance). A 2.7 deg/frame orbit moves the camera tens of cm per frame, so any one-frame lag fails by about 100x.
  - M = 0 and E <= 1 over at least 200 moving audits: the pool is not displaced; no origin fix exists to make. Then the rotation fronts must be the camera-fade cut: `rt fade class` in the turn windows, "in front inside the cut envelope" should take most of the pool front share, and "excluding faded occluders: primary match" should rise to about the static level (98.8-99%).
  - M > 0 with E of order camera step / tolerance: pool lag confirmed; report N, M, E per window, then fix where the matrices diverge.
- Report per window: primary match, in front, behind, miss; `rt epoch split` pool/soup front and behind; `rt fade class`; the mesh cache line (rejected audit mismatch count) and `rt passes: GPU cost`.
- Cost: audits add one captured draw and one small compute job each; not a quality change. No A/B key: nothing is changed.

Duration: plan 100 s; one launch, about 4 min (6 min timeout). Disk: logs only, about 10 MB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `pl1` and receipt `pool-lag.json`. Needs a build of the layer with the `[bvh] audits while the camera moved` line (branch pool-lag; layer change, game restart).

Offline check: `powershell -File tools\live_batches\pool_lag\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-10-31: all checks passed.
