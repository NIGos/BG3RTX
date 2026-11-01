# Replay ambient batch (live_batch replay-ambient), NOT authorized

Lean format of [../../lean_regression/README.md](../../lean_regression/README.md): one foreground launch per save, FG 2,
keys toggled inside the run, `rt_stats` only in the `diag-*` window, no PNG traces. Independent jobs (camp and inn
do not depend on each other). Receipt `replay-ambient.json`. READY_FOR_LIVE once authorized.

Question: is the game's native ambient (E x AO) brighter than the traced far field + sky (the PT reference) outside
S8 too? S8 (Water Queens House) gave E x AO / (k x PT far + env) 1.44, tiles p10 0.90, median 1.75, p90 3.01
(docs/PT_FEASIBILITY_OFFLINE.md). A systematic ambient correction needs an outdoor scene and an interior first.

Needs a layer + core build with the casters writer (this branch: layer/replay.cpp `casters`, bvhsplit.cpp per-slot
cascade record, core/gi.cpp `pipe_defines`; no ABI change) in the test tree, and a game restart.

| Job | Save | Scene tag | Plan | Why |
|---|---|---|---|---|
| `rc2-ambient-camp-fg2` | Campsite - 27h | camp | 45 s | S3 outdoor sun: far field and sky dominate |
| `rc3-ambient-inn-fg2` | The Blushing Mermaid - 96h 39m | inn | 45 s | interior: roofs (casters), local lights, probes |

Startup (both): `preset=ultra`, `rt_cascade_casters=1`, `rt_xfb_mb=1024`, `rt_cascade_mb=427`, `rt_hit_material=1`,
`gi_rt=1`, `gi_rt_radius=1.0`, `gi_rt_fallback=0`, `gi_hit_direct=1`; the pack contract's base variant:
`gi_hit_sun=0`, `sharc=0`, `rt_hit_emission=0`, `rt_hit_material_instanced=0`, `rt_omm=0`, `rt_native_*=0`;
`replay_pack_cap_mb=3500` (S8: 976 MB fixed + 2054 MB VT hit cache; the casters add 52 MB at camp and 84 MB at the
inn, 2026-10-31 logs; 3500 keeps the hit cache with about 400 MB to spare), `dump_images=0`.
Phases: `warmup` 15 s (exposure free), `hold` 10 s (`qa_exposure_hold=1`: the exposure is frozen before the pack),
`diag-replay-pack` 15 s (`rt_stats=1`, `replay_pack=2` camp / `3` inn: one pack at the first gi_hit_direct
dispatch of the window, with the live `GI hit direct` / `hit material` lines of the same window), `restore` 5 s.

## Gates
- Valid run: every expect line. Among them `[rrpost] exposure held`, the `[cascade] casters ... with the instance`
  line, `[replay] pack N casters: instance <i>, <n> triangles of stride >= 16, <MB> MB (soup of frame <f>)`, the
  recorded / written lines (shader reads >= 2: the layer's two dummies), the textures line with `s1b3+b14 2/2`
  (hit cache kept), `s2b34 1/1`, `s2b38 1/1`, `s3b1 1/1` under the 3500.0 MB cap, and `lighting inputs: 19 of 19
  slots bound` (probe cubes may be 0 outdoors: not gated). Forbidden: the pack failed / over budget, the casters
  `left out`, `exposure hold requested but no exposure read back`.
- Offline (post step, tools/live_batches/replay_capture/post_replay.ps1 `-Scene camp|inn`): `frame_replay --pack`
  (the note `casters: ... in the TLAS`), then `frame_replay --native-ambient` (PT_CASTERS from the pack, PT_SUN_LUT,
  PT_ENV, 32 iterations), then `py -3 tools/ambient_table.py --scene <tag> <pack>`. Gate: the comparison printed
  `native vs PT far field` and the casters went into the TLAS. Passed: older packs tagged with the same scene are
  deleted (file by file, never recursive); untagged packs (S8 142819-1) and the other scene's are kept.
- The answer: the two table rows (E x AO / (k x PT far + env), tiles p10 / median / p90, far-field share, sun /
  local) next to S8's. `py -3 tools/ambient_table.py <camp pack> <inn pack> <S8 pack>` prints them together.
- Cost: capture only, no rendering change, no A/B.

Duration: plans 45 s each; two launches, about 5.5 min with loads (5 min timeout each). Offline after each run:
about 1-2 min (pack load + 32 x 8 PT dispatches). Disk: each pack at most 3500 MB (about 3.1 GB expected), so
about 6.3 GB for the batch on top of the S8 pack (3.2 GB); the post step frees older packs of the same scene only.

Start, results and rollback: as in ../../lean_regression/README.md with this folder (`replay_capture/ambient`),
prefix `rc` and receipt `replay-ambient.json`; plans go to test-main `captures/live_batches/replay_capture/ambient/plans`.

Offline check: `powershell -File tools\live_batches\replay_capture\ambient\dryrun.ps1 [-Out <scratch dir>]`. Last
result 2026-11-01: all checks passed (estimate 5.5 min, 7020 MB). The post step's `-Scene` mode was checked on a
scratch tree: a synthetic box pack with lighting inputs and casters (`frame_replay --synthesize <dir> --box
--lighting --casters`) printed the native ambient lines and the table row, deleted the older `camp` pack and kept
an untagged and an `inn` pack.
