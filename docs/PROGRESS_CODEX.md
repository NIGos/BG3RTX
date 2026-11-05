# BG3RTX progress - 2026-11-05

## Report: optimisation night 2026-11-05 (main, release candidate rc-20261105-2aa5dcf8 PASS 12/12)

**Question for the user (only one): pubblichiamo la 0.9.1?** Files: `.scratch/rc-test/rc-20261105-2aa5dcf8-30de6495/`
(Setup.zip, Setup.exe, portable ZIP). Nothing uploaded.

**Qualified, always on** (in-run A/B at the inn, 600-frame windows, OFF repeats as noise):
- `rt_scene_compact_holds`: High scene build -0.87 ms GPU (OFF spread 0.075); energy within OFF spread; BVH identical by fixtures.
- `cpu_opt1`: layer hook bodies -0.13 ms/frame CPU (spread 0.016); BVH build -0.2 ms.
- `cpu_opt2`: -0.30 ms/frame CPU (spread 0.088). `cpu_opt3`: -0.24 ms/frame CPU (spread 0.054).
- Total CPU about -0.67 ms/frame at the inn on Ultra (layer hooks 12.4 -> about 11.7 ms/frame).

**Before/after on the release build (static, ms GPU, profile runs 00:09-00:21 vs 06:51-07:50, all VALID):**
| Scene | Preset | Scene build | GI block | Local shadows | Reflections | RR | Sum of these |
|---|---|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.19 -> 1.18 | 12.20 -> 12.10 | 0.69 -> 0.59 | 1.09 -> 1.10 | 2.60 -> 2.52 | 17.77 -> 17.49 (-0.27) |
| Water Queens House | High | 1.92 -> 1.27 | 2.06 -> 2.24 | 0.13 -> 0.12 | 0.41 -> 0.44 | 2.48 -> 2.48 | 7.01 -> 6.55 (-0.45) |
| Inn | Ultra | 2.45 -> 2.49 | 21.27 -> 21.18 | 2.03 -> 2.00 | 2.12 -> 2.11 | 2.53 -> 2.54 | 30.39 -> 30.32 (-0.07) |
| Inn | High | 3.42 -> 2.14 | 3.09 -> 3.07 | 1.07 -> 0.87 | 0.53 -> 0.58 | 2.61 -> 2.57 | 10.71 -> 9.23 (-1.48) |
| Camp | Ultra | 2.07 -> 2.06 | 17.82 -> 16.53 | 1.69 -> 1.64 | 1.63 -> 1.56 | 2.49 -> 2.43 | 25.69 -> 24.22 (-1.48) |
| Camp | High | 2.10 -> 2.25 | 3.87 -> 4.34 | 1.03 -> 0.97 | 0.46 -> 0.56 | 2.49 -> 2.52 | 9.96 -> 10.63 (+0.67) |
| Dialogue1 | Ultra | 1.76 -> 1.76 | 14.03 -> 14.12 | 1.35 -> 1.25 | 1.16 -> 1.18 | 2.61 -> 2.54 | 20.91 -> 20.85 (-0.07) |
| Dialogue1 | High | 2.14 -> 1.68 | 2.74 -> 2.80 | 0.80 -> 0.83 | 0.35 -> 0.36 | 2.50 -> 2.43 | 8.54 -> 8.10 (-0.44) |

Totals per preset (sum of the four scenes): Ultra 94.76 -> 92.88 ms (-1.88, mostly camp GI); High 36.22 -> 34.51 ms (-1.71,
mostly scene build at the inn and S8 from `rt_scene_compact_holds`). Single static windows from separate runs: differences under
about 0.3 ms per pass are run-to-run noise (camp High GI +0.47 is in a pass no qualified key touches: noise, not a regression).
CPU: about -0.67 ms/frame at the inn on Ultra (in-run A/B above).

**Release smokes (release profile, visible_unfocused_fg, all VALID):** Ultra S8 116 s; High S8 116 s (08:31, after removing gates
for water and cascade casters, which High does not run); stock-hair final dialogue 124 s.

**Release candidate:** rc-20261105-2aa5dcf8 PASS 12/12 (verify_rc: ZIP and setup). The merges after it (report, smoke-job fixes)
touch only `docs/`, `tools/` and `tests/`: no shipped file changed, so the candidate stays valid without a rebuild.

**Rejected with numbers** (code removed): gi_roof_reject (inn GI 21.75/21.58 OFF vs 21.87/21.95 ON), rt_origin_lazy_normal
(no saving), cpu_opt4 (+0.47/+0.54 ms CPU, slower). Dropped offline: GI temporal current-sample, reflection/local-shadow
early-outs (no exact proof or under 0.05 ms); ray flags already first-hit. Rounds stopped when a round found no win.

**Not reached:** Ultra GI trace (8-13.5 ms) stays the largest GPU cost; per-feature attribution needs a live tool that sees
cached pipelines (post-release). Next per user: bug fixing.

Details: [OPTIMIZATION_20261105](OPTIMIZATION_20261105.md), [RC_CHECK](release/RC_CHECK.md).

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).
- Optimisations that qualify are always on; no player option.

Full notes of the night: [archive](archive/PROGRESS_CODEX_2026-11-05-night.md); release 0.9 report: [archive](archive/PROGRESS_CODEX_2026-11-05.md).
