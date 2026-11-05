# BG3RTX progress - 2026-11-05

## Report: optimisation night 2026-11-05 (main, release candidate rc-20261105-2aa5dcf8 PASS 12/12)

**Question for the user (only one): pubblichiamo la 0.9.1?** Files: `.scratch/rc-test/rc-20261105-2aa5dcf8-30de6495/`
(Setup.zip, Setup.exe, portable ZIP). Nothing uploaded.

**Qualified, always on** (in-run A/B at the inn, 600-frame windows, OFF repeats as noise):
- `rt_scene_compact_holds`: High scene build -0.87 ms GPU (OFF spread 0.075); energy within OFF spread; BVH identical by fixtures.
- `cpu_opt1`: layer hook bodies -0.13 ms/frame CPU (spread 0.016); BVH build -0.2 ms.
- `cpu_opt2`: -0.30 ms/frame CPU (spread 0.088). `cpu_opt3`: -0.24 ms/frame CPU (spread 0.054).
- Total CPU about -0.67 ms/frame at the inn on Ultra (layer hooks 12.4 -> about 11.7 ms/frame).

**Before/after on the release build (static, ms GPU, profile runs 00:13/00:20 vs 06:51/06:55, all VALID):**
| Scene | Preset | Scene build | GI | Local shadows | RR |
|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.19 -> 1.18 | 12.20 -> 12.10 | 0.69 -> 0.59 | 2.60 -> 2.53 |
| Water Queens House | High | 1.92 -> 1.27 | 2.06 -> 2.24 | 0.13 -> 0.12 | 2.48 -> 2.48 |
| Inn | Ultra | 2.45 -> 2.49 | 21.27 -> 21.18 | 2.03 -> 2.01 | 2.53 -> 2.54 |
| Inn | High | 3.42 -> 2.14 | 3.09 -> 3.07 | 1.07 -> 0.87 | 2.61 -> 2.57 |
Single static windows; differences under about 0.2 ms are within run-to-run noise. Camp and Dialogue1 were profiled
(OPTIMIZATION doc) but not re-run after the changes. Stock-hair final smoke VALID (124 s).

**Rejected with numbers** (code removed): gi_roof_reject (inn GI 21.75/21.58 OFF vs 21.87/21.95 ON), rt_origin_lazy_normal
(no saving), cpu_opt4 (+0.47/+0.54 ms CPU, slower). Dropped offline: GI temporal current-sample, reflection/local-shadow
early-outs (no exact proof or under 0.05 ms); ray flags already first-hit. Rounds stopped when a round found no win.

**Not reached:** Ultra GI trace (8-13.5 ms) stays the largest GPU cost; per-feature attribution needs a live tool that sees
cached pipelines (post-release). The final Ultra+High S8 release-smoke job is refused by its own trace-window preflight
(tooling); the profile runs above served as the smoke. Next per user: bug fixing.

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
