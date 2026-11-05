## Exact early roof rejection (branch roof-on, kept, 2026-11-05)

- Qualify `gi_far_roof_early` (SpecId 1038) like `rt_compact_hit_state`: default/presets/release forced 1.
- `dev=1` preserves raw 0 for A/B; no player knob. Host statistics mode keeps the original path.
- S8 Ultra `opt-roof-s8.1.211159` VALID: GI trace OFF/ON/OFF/ON 7.655/6.275/7.937/6.226 ms (-1.38/-1.71).
- GI total 12.349/10.514/12.386/10.382 ms; RR about +0.2 ms in ON windows.
- Inn INVALID (last window short); complete windows about 12.2 ms ON vs 12.5/13.2 ms OFF.
- Offline paired: S8 -1.73 ms, inn -1.36 ms; camp neutral, 240 pairs +0.005 [-0.06,+0.06] ms.
- Exact (bit-equal). Request delta 0; ABI unchanged. Supplied live evidence; no new game/queue or git writes.
- Check S8 Ultra roofed-room far bounce: developer A/B leaves the image unchanged and reduces GI trace cost.
- Regenerated defaults/schema; settings checks 4159/4159 pass. Full build succeeds; CTest 192/193 pass (170.48 s).
- Remaining: `tests/gi_far_roof_test.py:67` expects cfg fallback 0; change to 1 in the owning lane (outside edit scope).
