# Offline opportunities (ranked, 2026-11-01)

Work that moves rendering quality without a game launch. Size: S (a day or less), M (a few
days), L (a week or more). Live tests still need explicit user steering (AGENTS.md).

**Do first: B, F, D.**

## B. Recipe extraction (M)
- Derive more native material recipes (`common/native_vt_surface.h` Recipe: shader pair,
  UBO offsets of transform, tile set and light channel) offline from the shader dumps and
  the asset DB's material tables.
- Why: hit material qualifies on under 1% of hits, so gi_hit_direct and rf_hit_shade barely
  participate. Each recipe widens every M4 consumer at once.

## F. Synthetic GPU fixtures (S-M)
- Run the HLSL hit and light code (native_hit_direct, bg3_boxes, native_hit_sun) on small
  synthetic inputs in a compute fixture and compare with the C++ references in `common/`.
- Why: catches formula and convention errors (AO factor, channel, box falloff) before a
  live batch; deterministic, cheap to keep in CTest.

## D. Asset-DB convention checks (S)
- `tools/assets/check_conventions.py` (docs/LIGHT_CONVENTIONS.md): flag/unit distributions,
  box ramps, roofed-room shares, per-camera light budgets.
- Why: answers "is it the lights?" for a live residual in minutes; found the box join
  denominator and the Kelvin x Color rule.

## A. Offline frame replay (L)
- Re-run our passes on a captured frame without the game.
- Needs a replay-pack capture mode first (lighting set 1, G-buffer, TLAS inputs, camera).
- Highest ceiling, but large; start only after B, F and D.
- Status 2026-11-01: harness `tools/frame_replay` + pack format + synthetic CTest done for gi_hit_direct
  (tools/frame_replay.md); live pack writer `replay_pack` built (ABI 25, READY_FOR_LIVE); rf_hit_shade's
  extra sets are next.
- First live pack (S8): 973 MB, giRaw 95.0% bit-equal with live, but hit material qualified 0: 4336
  image elements omitted (no TRANSFER_SRC), so hits failed as "sampling" (125705 of 189768).
- Texture read (2026-11-01, layer only, no ABI change): images without TRANSFER_SRC are read by
  `shaders/replay_image.hlsl` through a sampled view and stored decoded (BC1/2/3/7 as RGBA8, sRGB kept;
  BC4/BC5 as R8/RG8; BC6H as RGBA16F; `image ... SOURCE` names the game format). Self-test on the RTX
  5090: BC1/2/3/7 and BC6H bit-exact, BC4/BC5 within 0.5/255. Textures now fill the pack up to the
  2 GB cap; integer formats stay omitted.
- Second S8 pack (055812, 906 MB): no texture kept (`budget 4098`, `images copied 7, read by
  shader 1`; replay 94.9% bit-equal, qualified 0, sampling 125767). Real textures have TRANSFER_SRC
  (raw copies); the dropping order emptied the pack for a hit cache (b14[1]) larger than the 1142 MB
  left, and b14[0] was a second view of the alpha cache. Now (`rp::fill_budget`): the hit bindings
  first, smallest first, each image that fits; raw-compatible second views share the capture; a
  `textures` line measures every droppable image. Expected next pack: tables and the alpha cache
  kept, b14[1] dropped; the two caches exceed the 2 GB cap together, so qualified hits need either
  a larger cap for this job or a tile capture of the pages the hits sample (decision after the
  next pack's sizes). READY_FOR_LIVE: `rc1-replay-s8-fg2` (gates in its README).
- Third S8 pack (071930, 976 MB): tables kept, the hit cache s1b3+b14[1] dropped at 2053.7 MB (BC3
  sRGB 32688 x 26352 x 2, 2 mips). Decision (user, 2026-11-01): a larger cap for this one job. Ini key
  `replay_pack_cap_mb` (default 2048 = the old cap); the job sets 3300 (976 + 2054 = 3.03 GB) and
  expects `s1b3+b14 2/2`. frame_replay streams pack images in 256 MB pieces (no 2 GB host
  allocation; `--load-test` fills that cache shape on the RTX 5090 in 0.9 s, 2218 MB of a 32 GB heap).
  The job's post step replays the new pack and, if hit material qualified > 0 and sampling <= live,
  deletes the older packs (only the newest is kept).
- Lighting inputs (2026-11-01, offline, layer + tools only, no ABI or core change): the pack now also
  carries the deferred lighting's inputs as sets 2-3 (tools/frame_replay.md "Lighting inputs"): G-buffer
  albedo/material, AO, the sky irradiance and specular cubes, DFG LUT, the local probe records and the
  cubes they name, the atmosphere constants, transmittance LUT and clouds. Answers the PT reference's
  open observable (56-86% of the S8 bounce lies beyond the 1 m rays, where the game uses its native
  ambient): `frame_replay --native-ambient` replays the native diffuse ambient per GI pixel
  (tools/native_ambient.hlsl) and compares it with k x PT far field + the PT's sky at misses (mean,
  16 x 9 tiles); the PT also takes the sun from the game's LUT. Self-test 13-16 closed form; the S8
  number needs one new pack (the job's post step prints it). These inputs also unblock an
  rf_hit_shade replay contract later.

## E. Re-analysis of existing captures (S, low gain)
- Mine `captures/` logs and dumps for numbers not yet extracted.
- Most decisive data (join CSVs, b29 records) were not kept; expect little.

## C. Offline ground truth from M2 geometry (L, blocked)
- Path-traced references of a room from the offline M2 world data.
- Blocked on M2 room closure (roofs, state, materials still open); no closure claim from
  reference counts alone (RENDERING_EXECUTION_PLAN M2).
