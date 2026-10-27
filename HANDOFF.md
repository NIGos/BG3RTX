# HANDOFF: D-LS box (projector) light shadows (`ls_box`), PAUSED (WIP)

Branch `worktree-agent-a814985dd663a9b89`. Paused on request mid-implementation. The previous
HANDOFF (R0 sun replace) is in git history (`git show HEAD~1:HANDOFF.md`).

## State
- **Shaders: done, compile, not run in game.**
  - `shaders/rt/bg3_boxes.hlsli` (new): b29 record `_53` (offsets checked against the SPIR-V
    decorations of `captures/shaders/f469db7865d7c524.spv`: stride 128; `_m6` row-major rows at
    48/64/80; `_m8` 108; `_m9` 112), box uvw/inside test, box falloff (4 curves by `_m4 >> 24`),
    cookie colour (b45 bindless, SampleGrad with the quad uv differences, zeroed > 1/64 as the
    game), class terms `bg3_box_eval_mat` (copy of `bg3_light_eval_mat`'s tail: Ld = `_m9`,
    norm 1, `_m1 & 1` → b13[0]), the box atlas term (orthographic: bias 3.5·`_m4`, ref
    `((z − m5.y)/m5.x)/m6`), and `bg3_box_exit` (ray tmax = exit of the box on the light side).
    It reads the game's set 1 as **set 3** (b29, b16 sampler, b45 cookies).
  - `shaders/gi/rt_local_shadows.hlsl`: everything under `#if LS_BOX`. Default variant and
    exact-k: `ls_box_all` (3 phases like the point/spot loop). LS_FULL: inline loop with
    validation (`md/ms` include box lights), debug views, stats counters 136-150 (box evals,
    shadow-mapped, traced, occluded, unknown, game box light, light removed, cookie, projector-px
    validation 144-148, box share 150). `vpx` = all pixels with LS_BOX. Counters 144-148 are
    also counted with LS_BOX=0 (baseline for the comparison).
  - Wrappers `rt_local_shadows_box.hlsl`, `rt_local_shadows_full_box.hlsl`.
  - **Verified: the LS_BOX=0 default shader's SPIR-V is byte-identical to HEAD's** (so ls_check
    for point/spot is unaffected); all 7 D-LS shaders compile with DXC (merge-green flags).
- **Host (`core/gi.cpp`): started.** Added globals `g_ls_box`, `g_ls_full_box`,
  `g_ls_box_layout`, `g_ls_box_game` and `ensure_ls_box_layout(game)` (sets: push 0, host alpha
  1, sun 2, game set 1 as 3). Nothing calls them yet: **ls_box has no effect yet.** Build passes.

## What's left
1. `run_post` (≈ line 1745): if `cfg ls_box` and `s.game_set1` and not `ls_check`/`ls_impl=0`
   and `ensure_ls_box_layout(s.game_set1_layout)`: refresh `g_ls_box` / `g_ls_full_box`
   (`gi/rt_local_shadows_box.hlsl` / `_full_box.hlsl`) with `g_ls_box_layout`; dispatch with
   that layout and also bind `s.game_set1` at set 3 with `s.game_set1_dyn` (as `rf_dispatch`).
   Fall back to the normal pipeline otherwise; log once on (and once when set 1 is missing).
2. `kStatsCounters` 136 → 152; an `ls box stats` log line (box listed/evaluated/shadow-mapped/
   traced/occluded/unknown/cookie per px; game box light and removed light as % of t[21]; the
   projector-pixel validation 144-148 and 150); add `ls_box` to the GPU-median timing line.
3. `gi_release`: destroy `g_ls_box`, `g_ls_full_box`, `g_ls_box_layout`; reset `g_ls_box_game`.
4. `bg3rtx.ini` / settings defaults: `ls_box=0` (check `scripts/gen_settings_defaults.py`,
   `docs/USER_SETTINGS.md`).
5. Docs: TRACK_D_RT.md 9.12 (design, results), COVERAGE gap 2 update, QA.md if needed.
6. Unit tests + `merge-green` checks locally; queue jobs (none enqueued yet): per scene S6, S7,
   S5 a session with `ls_box=0` / `ls_box=1` × (`ls_stats=1`, `ls_debug=4` + `ls_stats=1`,
   `ls_check=1` with ls_box=0), `hold` ≥ 90, patterns `ls stats`, `ls box stats`,
   `ls validation fit`, `ls check`, `local-light shadow pass GPU median`.

## Open questions / design notes
- No source angle for box lights (directional, hard shadows): no derivation for one exists.
- Box faces embedded in walls would occlude the whole light: watch per-scene occlusion share.
- Box lights have no per-light data (join covers b25 only); `ls_noshadow=2` cannot apply.
- Cookie LOD uses neighbour-pixel uv differences, not the game's exact quad lanes.

## Jobs enqueued
None.
