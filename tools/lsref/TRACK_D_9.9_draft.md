### 9.9 Cost: where the time went, and the optimised pass (2026-10-26)

Status: **on by default**, no setting to choose. The output is **bit-identical** to the
pass before (proof below). Code:
- `shaders/gi/rt_local_shadows.hlsl`: the default-settings variant (`LS_FULL=0`) in three
  phases, and the old single loop as `LS_FULL=1` (`rt_local_shadows_full.hlsl`), which
  `core/gi.cpp` runs only for `ls_samples`, `ls_shadowed`, `ls_debug` or `ls_stats`;
- `shaders/gi/rt_local_shadows_match.hlsl`: the join keeps unchanged slots;
- A/B tooling: `shaders/gi/ls_ref/` (the pass of `f5e9eab`, frozen by
  `tools/lsref/make_ls_ref.py`), `rt_local_shadows_ref.hlsl` (`ls_impl=0` runs it),
  `rt_local_shadows_check.hlsl` (`ls_check=1/2`), `tools/pipeline_stats.py` (driver
  register counts, no game needed).

All numbers: RTX 5090, 2560x1067, GPU timestamps around the join and the shadow pass
(the core now logs the join separately), medians of 300 frames, `-Background` runs with
live setting phases (`scripts/live_phases.py`), the first window after each switch
dropped. The GPU was shared with the user's desktop (another game at times), and the
foundry ran at 24 fps with `rt_bvh_split=1`: absolute values drift by about ±10% between
runs (the old pass measured 0.55-0.77 ms in the same save), so every comparison below is
within one run.

**Where the time went (old pass, foundry, one run; each switch from the default):**

| Configuration | ms | vs default |
|---|---|---|
| default | 0.77 | |
| no rays at all (`ls_min_radius=1e5`) | 0.37 | rays −0.40 |
| `rt_origin=0` | 0.70 | −0.065 |
| `ao_direct=0` (no sun recompute, no AO give-back) | 0.70 | −0.07 |
| `rt_bvh_split=0` (the frame then ran at 130 fps) | 0.69 | −0.08 |
| `ls_soft=0` | 0.73 | −0.035 |
| `ls_lights=0` (no join; the character light traced again) | 0.77 | 0.00 (join −0.011, character light +0.01..0.07) |
| `mat_classes=0` | 0.77 | **0.00** |
| `ls_owners=31` (character light traced) | 0.84 | +0.07 |
| `ls_samples=1` | 0.57 | |
| `ls_lights`, `mat_classes`, `rt_origin`, `ao_direct` all 0 | 0.64-0.66 | −0.12 |
| ... and `rt_bvh_split=0` | 0.59 | |
| `rt_alpha_test=0` (another run, default 0.59) | 0.535 | −0.06 |

- **The per-feature switches explain only part of it.** `mat_classes=0` costs nothing to
  turn off, although the per-class terms were the largest audit change: the price of
  the per-class code is paid in **registers**, whether a class is used or not.
- **Registers** (`tools/pipeline_stats.py`, the driver's own count): the old pass used
  **252 registers and 16 bytes of local memory** per thread, so 8 warps per SM (of 48).
  The ray queries, the alpha test at their candidates and the generic per-class BRDF
  were all live in one loop. A pure ray pass (`rt_shadow.hlsl`) needs 72.
- The rays themselves (about 0.25 ms after the optimisation) did not get cheaper: 2.08
  per pixel, the ray origins (`rt_origin`) and the BVH split's instance lookups at alpha
  candidates make them longer, and exactness forbids shortening them.

**What changed (all exact):**
1. **Two variants.** Every debug view, statistic, the RIS reservoirs and the
   shadow-map modes live only in `LS_FULL=1`. Nothing but the four sums the output needs
   is live in the default variant's loops.
2. **Three phases per 32 list entries** instead of one loop:
   1. *Which entries are traced*: the trace conditions first (no game shadow map,
      `ls_min_radius`, owner from the join), then `bg3_light_eval_mat`'s own acceptance
      tests (channel, range, cone, N·L or the class's transmission/hair test), from the
      few surface terms they need. The BRDF is no longer evaluated for the lights that
      are not traced (the character light alone was 16.6% of the evaluations).
   2. *The rays*, the k-th traced light of every lane together, with only the ray
      inputs live.
   3. *The rest of the surface* (reloaded at an index the compiler cannot prove equal to
      the pixel, so it is not kept across the rays), each traced light's exact
      unshadowed diffuse and specular, and the sums in list order with the old
      expressions.

   Result: **96 registers, no local memory, 21 warps per SM.**
3. **The sun is not recomputed where it cannot matter.** With `ao_direct=1` it only
   enters as `(1 − fdif)·sun`; where both AO factors are exactly 1 that is 0. Where the
   sun mask is 0 the cloud-shadow lookups are skipped (0 × a finite factor).
4. **The specular-AA neighbours**: the pixel's own normal is reused (2 normal loads instead
   of 4; the clamp at the odd last row behaves as before). `gAux` is read only in mode 2.
5. **The join keeps unchanged slots.** Each slot caches the inputs of its result (absolute
   position and radius bits, and a generation the core bumps when the uploaded records
   change); a group whose slots are all unchanged skips the search. 0.011 → 0.005-0.007 ms.

**Tried and dropped (measured, not kept):**
- Rays to the wave's union of list positions (rays to the same light together): +0.007 ms.
- The rays of the first 32 entries as their own dispatch (72 registers there): +0.07 ms;
  the second pass reads the G-buffer and the list again.
- No ray for lights of colour 0: exact, but no such light is traced here; no change.
- Skipping rays to lights whose unshadowed term stays below 1/256 or 1/64 of the
  pixel's (not exact): no measurable gain, and the extra evaluation raised the default
  variant to 125 registers (+0.03 ms).

**Not tried, or not applicable:**
- Wave-scalarised light loops and packed per-light caches. A wave's pixels mostly share one
  froxel list, the light records are read from L1, and the no-ray time is now 0.14 ms in
  all; the registers were the limit, not these loads.
- Ray flags: `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` was already set, and `tmax`
  already stops `max(R, ls_clear)` short of the light. Back-face culling would change the
  image (back faces occlude too).

**Result (foundry, same runs, new / old):**

| Run | new | old (`ls_impl=0`) |
|---|---|---|
| A/B 2 (4 + 4 alternating phases) | 0.388-0.401 | 0.566-0.595 |
| A/B 6 | 0.409-0.417 | 0.59-0.65 |
| A/B 6, `rt_bvh_split=0` | 0.377 | 0.563 |
| no rays (`ls_min_radius=1e5`) | 0.143 | 0.372 |
| join alone | 0.005-0.007 | 0.011-0.013 |

- **−0.18 to −0.23 ms (−31 to −37%)** at the same image.
- **The 0.35 ms target is not reached** under these conditions: 0.39-0.42 ms against an
  old pass of 0.57-0.65 ms in the same runs. Scaled to the 0.59-0.63 ms the old pass logged
  when this work started, that is about 0.40 ms. The remaining time is the rays
  (≈0.25 ms) and the G-buffer traffic of a full-resolution pass (≈0.14 ms with no ray at
  all). Reaching 0.35 needs fewer or shorter rays, which changes the image (for example
  `ls_samples=1`, or a lower resolution for the visibility).
- Under the 0.5 ms budget (QUALITY R7) again.
- Per feature, new pass (A/B 6): `rt_origin` +0.045, `ao_direct` +0.026, `rt_alpha_test`
  +0.015, `ls_soft` +0.012, `mat_classes` 0, the join + owner skip −0.04 (`ls_lights=0`
  traces the character light), `ls_owners=31` +0.056.

**Other scenes** (`-Save`, same tooling: a check phase, then new and old alternating,
three phases each):

| Scene | new | old | change | `ls_check=1` |
|---|---|---|---|---|
| Steel Watch Foundry (default save) | 0.39-0.42 | 0.57-0.65 | −31 to −37% | 0 px differ |
| Grymforge (`Grymforge - 32h 59m`, torches and lava) | 0.71-0.73 | 0.97-0.98 | −26% | 0 px differ (2.29·10⁹ px) |
| Water Queens House | 0.114 | 0.169 | −33% | 0 px differ (6.23·10⁹ px) |

Grymforge is above the 0.5 ms budget in both: it has many more traced lights (torches
along every wall). That is a ray count, not a regression of this change.

**Image equality.**
- `ls_check=1` runs the frozen old pass and the live one on the same pixel, frame and
  inputs in one dispatch, compares the float outputs bit for bit, and logs the counts per
  120 frames (`ls check`). It also searches every join slot and compares the kept results.
- Foundry, default settings: **0 pixels differing in any bit** over every window of every
  run since the last change (for example 2.62·10⁹ pixel evaluations in A/B 3, 1.97·10⁹ in
  A/B 6), and **0 kept join slots differing** from a new search.
- Also 0 with `ls_owners=31`, `ao_direct=2`, `rt_origin=0`, `ls_soft=0`,
  `mat_classes=0`, and for the `LS_FULL` variant with `ls_samples=1` and `ls_debug=2`.
- `ls_check=2` compares the old pass with itself (a second evaluation the compiler
  cannot merge): 0 over 16 windows. So the inputs do not change under the pass, and 0
  means equal.
- The `LS_FULL` check showed one window with differences in three runs, each time the
  window in which a setting switched variant (`ls_samples` or `ls_check`); the following
  windows are 0.
- A first version was not exact: it wrote the `ao_direct=1` give-back as
  `dir·(1 − fdif)` with a constant 1, and 2.1% of pixels differed in the last bits
  (17 px per 120 frames above 1e-5 relative). Keeping the old expression
  `dir·(td_f − fdif)` made it exact. The check is what found it.

{VALIDATION}

Settings (all live): `ls_impl` (1 the optimised pass, 0 the frozen old one), `ls_check`
(0 off, 1 old against new, 2 old against itself; measurement only, about 1.5 ms).
