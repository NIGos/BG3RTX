# BG3RTX progress - 2026-11-20 (main 08cb1e99)

Live tests: paused by the user on 2026-11-20 ("Non ora"). A new session must ask again, and tell the user right before
launching the runner. Pending live evidence is READY_FOR_LIVE, not a pass.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). Release check: [RC_CHECK](release/RC_CHECK.md).
Full text of the 2026-11-15 and 2026-11-20 entries: [archive 2026-11-20](archive/PROGRESS_CODEX_2026-11-20.md); older:
[2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md), [2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md), [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039). The user played it on 2026-11-20 and found everything fine.

## 2026-11-20: release candidate 0.9.6-beta rebuilt from main 08cb1e99

- `dist/BG3RTX-0.9.6-beta.zip` (230 files), `-symbols.zip` and `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe`
  (unsigned); `VERSION.txt` commit 08cb1e99; `SHA256SUMS.txt` regenerated. Hashes in RC_CHECK.
- verify_rc: 11 PASS, Setup SKIPPED (another install present: 0.9.6-hairtest18). Older candidates in `.scratch/old_rc_745a1fff/`.
- The packaged `bg3rtx.ini` carries the five new keys at 0. Nothing published: publishing is the user's action.

## 2026-11-20: two consolidation rounds (final-* then r2-*, offline, merged)

### Host layer (claude/final-layer, claude/r2-layer)
- Source: the user's log of 16:45-16:50 (hairtest18, Ultra, FG 2x, RTX Hair toggled live): no `VK_ERROR`, peak 27.1 GB used
  (ours 8.7-15.3 GB), 162 BLAS chunk refusals (137 led by the cascade casters), 17.4 s of strand-variant pipeline creation.
- Groom decode fix (on): the pool range is taken before the decode, so a refused growth no longer repeats a 30-190 ms
  decode (two LODs were decoded 9 times). Check: no LOD id with several "decoded" lines before its "uploaded" line.
- Tracker (on): freed sets and the sets of reset or destroyed pools leave the descriptor maps (descriptor_epoch_test);
  `DestroyBuffer` / `DestroyImage` / `DestroyImageView` erase their records before the driver's destroy (a reused
  handle on another thread lost its `buffer_mem` record).
- Logs (always on): `[mem] ours` splits the BLAS chunk reserve; `[bvh] BLAS refusals in N frames` once per 300 frames when there were refusals.
- Cause of the refusals: small ranges settle in the large cascade and groom chunks and pin 100-190 MB each, so a
  cascade rebuild needs a new chunk past the cap and the casters are left out.
- New keys, all 0, live, READY_FOR_LIVE:
  - `rt_as_size_class`: base-size ranges only in base chunks. Replay (8 seeds x 1380 frames): refusals 53 -> 2.
  - `rt_cascade_keep_refused`: on a refusal the previous cascade BLAS is traced (rebased) instead of no casters.
  - `rt_hold_pack_base`: the 24-byte hold pack also for the 64-byte layout. Predicted 91.5 -> 34 MB per static build.
  - `rt_hair_header_early` (startup): the strand header address is stable across launches, so the 519 strand variants
    hit the driver cache. Pipeline creation 14.7-48.5 s at launch -> under 1 s predicted on the second launch.
- Staged jobs: `tools/live_batches_staged/final_layer/` (fl-s8-holdpack, fl-header-early: two launches),
  `tools/live_batches_staged/r2_layer/` (r2-inn: inn dolly, both chunk keys 0/1/0, 8.6 min; at this PC's cap refusals may be 0 in both states).

### Core and shaders (claude/final-core, claude/r2-core)
- Dead code out: `GI_DEBUG_SWIZZLE`, `GI_FAR_M`, the unreachable `kCascade` branch, spec id 1012 (retired). 225 / 225
  requests byte-identical. `RT_SCENE_REBASE` / `RT_REBASE_LIGHT_CAMERA` kept (a hair probe tool compiles them).
- `refresh()` reads its cfg keys once per frame index (`core/frame_memo.h`, CTest `frame_memo_test`). No ABI change.
- giRaw clamp: a NaN now stores 0, overflow and +Inf 65504 (not reachable today; only the 28 hit-GI requests change).
- Skipped unread work (image identical): sun roof-volume test only where the resolve reads it; RF probe term skipped
  at w = 0 (replay -0.034 ms); GI temporal in RR mode writes the fresh sample directly; water_primary / water_reflect
  run only with a reader. Two early-return paths in debug dispatches now restore the game's bindings.
- `gi_far_roof_emission` (spec constant 1054, 0, live): the far roof rule also for a hidden hit's emission beyond the base
  radius. Off = old program (6 / 6 hit_sun_em requests equivalent). Staged: `tools/live_batches_staged/r2_tools/`
  r2-inn-roof-em (inn, key 0/1/0, then a GI replay pack for the PT reference). Predicted: GI energy lower or equal, closer to PT.
- 238 / 238 CTests. `shaders/variants.txt` delta 0 in both rounds. No ABI change (31).

### Launcher, docs and tools (claude/final-tools, claude/r2-tools)
- Launcher: the GPU list no longer closes on every 2 s refresh; a WebView page that fails to load now falls back to the
  native window (was exit 2, no window); a preset change names three moved settings, the rest in a tooltip; the
  preparation text says it runs while the window is open. 21 views x 3 sizes, 0 layout problems (headless Edge).
- verify_rc: an existing install makes the Setup row SKIPPED (exit 2), naming the version from the install's `VERSION.txt`.
- `release_shader_equivalence.py`: a crashed spirv-opt is named (stage, passes, exit code) and retried once; the first
  difference decides a request (74 s instead of a 13 h cross); `--current REV` compares a commit with its parent.
- Python CTests whose cases skip report Skipped. The dlss5 staged job is committed (lint 0 failing). Release notes,
  Nexus texts and RELEASE_EVIDENCE name the groom decode fix and the 15-50 s strand-variant creation at start.

### Known, not fixed (owner or A/B needed)
- Water game shadow at Vg = 0 (`common/water_fs_patch.h:699`): Dg is NaN, the pixel keeps the game colour. No small fix
  (D_u needs the cloud-layer chain rebuilt with Vg = 1); a guard changes no pixel.
- `rr_post_temporal.hlsl:51` rejects most history under camera motion (energy-safe). `rr_post.hlsl:250` (non-temporal
  post, Low-High) lacks the jitter offset. Both image-changing.
- rt_shadow waits for the whole GI chain (`core/gi.cpp:3650-3672`); recording it earlier may save 0.05-0.2 ms (GPU A/B).
- GI ray cone `pa * 2.0` (`gi_trace_rt.hlsl:141, 313`) is one mip too coarse in RR mode (full resolution). Small.
- Validation status ring can wrap onto a waiting batch (detector `[bvh] status ring wrapped` only; never seen).
- The mesh pool cannot shrink while a groom sits at its end. Pixel-LOD camera BLAS not compacted (about 16 MB per groom).
- 282 draws re-captured and refit every frame with a static camera at S8. `stats_enabled()` and ~100 raw cfg lookups
  in run_pre/run_post (about 40 us per frame).

## 2026-11-15: release 0.9.6-beta prepared (claude/release-prep, claude/leftovers-small)
- User decision: finalise for release, RTX Hair Off by default. Settings audit found no inconsistency: 23 controls,
  4 behind "Show experimental options", 32 developer knobs hidden. `test_release_rtx_hair` pins RTX Hair Off in all presets.
- Release notes, evidence and Nexus changelog written; verify_rc fixes (`py -3`, 230 files). Final smoke staged in
  `tools/live_batches_staged/release_final/` (`final-s8-ultra-high`, `final-stock-hair-dialogue1`, about 7 minutes).
- Leftovers: licence files keep their bytes (`.gitattributes`); `tools/spec_omm_alias_probe.py` and
  `tools/spec_constant_proto.py` landed. Cost 0 ms.

## 2026-11-14, night (one line each; full text in [archive 2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md))
- VRAM right-sizing: `rt_capture_demand_size`, `rt_capture_peak_reserve`, `rt_capture_split_mb`, `rt_mesh_pool_trim` On by default after vr-s8 / vr-inn: -0.8 GB at S8, -1.0 GB at the inn, 0 faults, energy within noise. `rt_as_chunk_fit` stays 0.
- Layer consolidation: elastic capture growth (ten 1.4-1.8 GB reallocations -> three); `rt_hair_groom_async` (0, READY_FOR_LIVE); shader scan every 5 s in release; Streamline stale-layout erase.
- Core consolidation: GI images no longer reallocated twice per level load (`core/rr_mode_latch.h`, READY_FOR_LIVE log check); `probe.hlsl` and `rr_hair.hlsl` removed.
- Launcher and tools: refresh sends only changes, slider drag fixed, 58 fps start limit, converter at idle priority; 22 tests added to CTest; live lint counts active jobs only.
- Disk cleanup with the user's approval (about 311 GB, merged branches); the E: free-space reading stayed 482.3 GB (unexplained).

## 2026-11-14, day (one line each; full text in [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md))
- RTX Hair instability under camera motion solved for converted styles (26-key bundle, hx1 202905: 0 strands->cards). Still Off in every preset.
- Open for hair: styles without a groom file stay cards; hitch frames fixed offline only; `rt_hair_card_fill` Advanced with no visible gain.
- Scene handout ("Low-latency scene") On in High and Ultra with the hit wait: scene wait 3.5 -> 0.3 ms (S8), 8.5 -> 2.9 ms (inn).
- Sensible gates (`tools/live_batches/sane_gates.py`): FAIL only beyond the OFF noise plus a floor; an unpredicted drop is EXPLAIN.
- Performance: release profile not slower than 2026-11-05; VRAM guard LOW latch fixed; launcher "Direction A" merged.

## Earlier hair notes (2026-11-05 to 2026-11-11)
- Motion stability On in High and Ultra (`rt_hair_mv_card=2`). Pixel LOD (Off / 1/4 / 1/8): strand G-buffer -27% / -34%.
- Strand density: the tips dither is the measurable coverage loss; `rt_hair_groom_tips=0` kept (2026-11-07).
- Primary-only minimum strand footprint rejected offline (2026-11-07). ReShade rs1 INVALID (ReShade not loaded, 2026-11-05).

## Current user decisions
- Stock visible hair ships; RTX Hair is an opt-in Beta control (Lighting > RTX Hair, Ctrl+Shift+F7), Off in every preset. Experimental page in release (behind Show experimental options, all off): Water surface reflections, two Frame Generation pacing tests, Strand hair edge resolve.
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only.
- 2026-11-14: Low-latency scene On in High and Ultra, always with the hit wait fix. Sensible gates.
- 2026-11-14 night: the four VRAM right-sizing policies On by default; disk cleanup approved.
- 2026-11-15: finalise 0.9.6-beta for release, RTX Hair Off by default. 2026-11-20: lives paused ("Non ora").
- Pending: RTX Hair On in a preset; `rt_hair_card_fill` stays an Advanced option unless the user wants it.

## Older entries (2026-11-04 to 2026-11-09; full text in the archives [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md), [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md))
- Releases: 0.9.3-beta, 0.9.4-beta (RC PASS), 0.9.5-beta packaged 2026-11-08; build.ps1 prefers VS 2022; packaging on a mounted-folder disk; launcher picks the RTX GPU.
- Scene handout design and increments 1-5 (2026-11-08/09), mesh cache budget charged with its own BLAS, BLAS chunk caps, VRAM reserve from the observed rise.
- RTX Hair 2026-11-04 to 11-09: groom storage v3 and converter `bg3rtx_hairconv`, swap and join fixes, strand fibre shading, edge quality, warm-up,
  switchable while playing (Ctrl+Shift+F7), header camera from the GPU, strand-resolve pairing, capture stride split, M1-M5 quality items (mostly READY_FOR_LIVE).
- Lighting: RT pipeline GI trace (on), compact GI hit state (always on), early roof rejection (on), exact local-shadow optimisation, RF probe work (mixed),
  SER feasibility, wide split on by default, rise reserve on, Home dashboard and Hotkeys page.
- Rejected: far GI live state, conservative GI-only TLAS, GI origin audit, BLAS flags replay, sun wave, default Ultra RF follow-ups.
- Audits (fixed offline): DLSS 5, FG, core completion contract, L11/L12/L17 submission, suspect review, S-F05 R11G11B10, ReShade isolation, queue cancellation.
