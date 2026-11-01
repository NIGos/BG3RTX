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

## E. Re-analysis of existing captures (S, low gain)
- Mine `captures/` logs and dumps for numbers not yet extracted.
- Most decisive data (join CSVs, b29 records) were not kept; expect little.

## C. Offline ground truth from M2 geometry (L, blocked)
- Path-traced references of a room from the offline M2 world data.
- Blocked on M2 room closure (roofs, state, materials still open); no closure claim from
  reference counts alone (RENDERING_EXECUTION_PLAN M2).
