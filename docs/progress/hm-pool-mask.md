## Pool hit materials: why 14.9% of S8 lookups have no row, Base_MSK_VT rows (branch hm-pool-mask, READY_FOR_LIVE, 2026-11-01)
- Cause of "pool without row" (S8 replay, 14.9% of lookups, 133 instances): class A pairs with no recipe ("other
  pair" 67 draws / 172 instances in the live log; basis ready 99/100, slots free). By S8 trace draws/frame:
  HeightGradient 54df894a/7a07134e 17.9, Tileset 6044c8cf/02807a68 13, then five smaller pairs (MSK 5).
- Built the smallest: Base_MSK_VT 2d2ac3c3/a5f44c48 (`LayoutMask`, live key `rt_hit_material_mask=0`, GUI
  Experimental). Classifier vs 14678345: only RT1 (albedo tint by a 2D colour mask and three colours) and RT2.y at
  byte 12 differ (tests/material_families_test.py BaseMask; native_hit_pool_test pins the recipe and nv_mask_albedo).
- Tileset needs per-vertex COLOR in the pool basis; HeightGradient needs world height and more than 256 B of row.
  Both are infrastructure; not started.
- Offline: build + 91/91 CTests; key-off S8 replay bit-identical (fnv1a a54c4dfd1cc9acb9, all counters equal) with
  the new SPIR-V. The after-share cannot be replayed (pack rows/views are live captures). Shader identity vs 89dd500
  PENDING: stopped at 112/1464 (0 failed) when the live batch started; rerun when no bg3.exe (resumes from the cache).
  Expected: only requests including native_hit_material.hlsli change (an extra branch that does nothing without mask rows).
- Live batch `tools/live_batches/m4_hit_mask` (S8 + inn, 2 x 144 s, ~9 min, 0.4 GB): dry run passed. Gate: pool
  pairs line lists a5f44c48 R/B > 0 in diag-on, absent in diag-off; capacity and scene-wait 0. Predicted S8 +1.1 pts.
- Note: the first baseline frame_replay ran while a bg3.exe was up (it had exited a minute later); later runs checked.
