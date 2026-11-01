# Session handoff - 2026-11-01 evening (PAUSED by the user)

Main f87c042, ABI 25. Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next, then [GATE_AUDIT_20261101.md](GATE_AUDIT_20261101.md)
and [META_AUDIT_20261101.md](META_AUDIT_20261101.md). Live tests are paused until the user says otherwise.

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>` (junctions to `external/`,
  `data/assetdb`; careless removal emptied the shared SDKs twice).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
- No builds, shader compiles or GPU tests during a live batch's cost windows.
- Gates hold in every ON window (`expect_every`). Stats counters device-local (`host_atomics_lint_test`).
- Branches write `docs/progress/<branch>.md`, never PROGRESS; the coordinator folds them.
- Merge only with `scripts/merge-green.ps1`; features off by default until their gates pass.

## Running now (pause)

- Live tests suspended (coordinator flag; queue empty, no bg3.exe). No feature work until the user resumes.
- Maintenance only: shader tooling (`tools/shader_identity.py`: content-addressed cache + progress, replacing
  the seven `tests/*_shader_test.py` blind recompiles) and an audit of the other long tools (progress, caching).

## Branches not merged (all offline-verified; each still needs the noted step)

| Branch | What | Still needed |
|---|---|---|
| worktree-agent-a05080ba75170ea35 (0be532f) | `rf_hit_gi`: GI bounce at reflection hits (S8 bias 0.925 -> ~1.00 expected) | shader identity gate; m4p3/m4p4 run |
| worktree-agent-a87dc09be59810f7c (b22c94c) | `rt_hit_material_wind_rgb` third instanced family (S8 +13 pts expected) + blocker ranking tool | shader identity gate; `m4_wind_rgb` run |
| worktree-agent-a4f18c6ce7ff7c559 (70b7c53 WIP) | water shadow-sampling families + `rt_water_game_shadow`; camp water job | compile new compose variants, byte identity, m5_promotion dryrun |
| worktree-agent-a9e0aabd4d7f18008 | M4 bundle on in Ultra | do not merge until m4_promotion passes |

Warning: the first two branches BOTH allocate rtStats 419..426: renumber one before merging (table next to
kStatsCounters in core/gi.cpp).

## Bugfix round (user, 2026-11-01 evening) - before any feature work

Found by the user playing with every experimental key on (bg3rtx.log 17:11-17:31):
1. **Stale scene with hit materials:** `[rt] scene wait` 456 timeouts / 600 (mean 12 ms vs ~2 ms without
   `rt_hit_material`): shadows detach on rotation/pan and flicker. Agent on it (move the provider's work off
   the scene-commit critical path; scene-wait gate in every job).
2. **Water does nothing in Ultra:** `core/water.cpp:1066` skips every water pass when `rt_cascade_casters=1`
   (Ultra default); every water job ran with casters off. Needs the cascade-mask variants.
3. **Capture buffers too small with hit materials** (64 B vertices): `rt_xfb_mb` 384 / `rt_cascade_mb` 160 dropped
   roof casters (86-132/frame) and scene draws. User ini raised to 1024/427; the default must follow the key.
4. **Gates never tested the real preset with features combined:** add an Ultra "all qualified experimental on"
   smoke job (capacity 0, scene-wait timeouts 0, water participation > 0 in every window).
5. rtStats 419..426 claimed by two branches (rf_hit_gi, wind_rgb): renumber before merging.
6. FG off when the desktop resolution differs from the game's in fake fullscreen (layer size match).

## Next, in order (after the pause)

1. Land the shader tooling; rerun the identity gates of the branches above; merge them in one merge-green.
2. Live (needs user go): `m5_promotion` (S8 + Flymm + camp) -> water in Ultra if it passes; `m4_promotion/s6_diag`
   (S6 darkening = animated lava + compile stalls, offline); `replay_capture/ambient` (camp + inn packs, ~6 GB kept
   as references) for the native ambient / reference ratio; `gi_far` gf2 diag was run 16:14 (analyse with the
   updated gate tool: exposure-corrected); m4p3/m4p4 with `rf_hit_gi`; `m4_wind_rgb`; box lights visual check.
3. `rf_hit_shade` stays experimental until its bias is within the game's spread (user decision); S6 excess is in
   the direct and box terms, next fix there.
4. Release-hardening (user asked about a Nexus beta): release defaults, FG with a desktop resolution different from
   the game's, acts 2-3, long sessions, installer, README, licences.

## Live batches prepared (not authorized)

Under `tools/live_batches/`: `m4_promotion` (gate table `python -m tools.m4_promotion_gate`), `m4_instanced`,
`m5_water` (m5w2-m5w6), `gi_far`, `replay_capture`, `em_forge`, `lean_regression`. Each README states its gates.
Ask the user for a concrete batch once its offline checks are done.

## Working pattern

- One hypothesis, the smallest change behind an ini key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass from the `gpu` log lines.
- Record a disproved hypothesis in the batch that disproved it.
- Keep this file under ~150 lines and PROGRESS_CODEX under ~150; archive older sections.
