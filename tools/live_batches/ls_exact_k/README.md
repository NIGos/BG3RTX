# ls_k live batch (live_batch ls-exact-k), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the game takes focus and the pointer: the user must be away), FG 2, the key toggled inside the run, 20 s diag-off `cost-*` windows, one PNG per 8 s `shot-*` phase, no float bursts. Receipt `ls-exact-k.json`; the plan goes to test-main `captures\live_batches\ls_exact_k\plans`.

1. `lsk1-exact-k-inn-s7-fg2` (The Blushing Mermaid - 96h 39m, S7 inn: candles and lamps, many local lights per pixel; 147 s). [TRACK_D_LOCAL_LIGHTS](../../../docs/TRACK_D_LOCAL_LIGHTS.md) 9.11 `ls_k` (re-added as the LS_K variant). Warm-up OFF, then `diag-precheck-k1` (15 s, `ls_k=1`, `ls_stats=1`): its `ls exact-k stats` line needs pixels over the cap (`k-hit` above 0.0%), so a view with at most one traced light per pixel is INVALID, never a pass. Then OFF-a / k1 / k2 / OFF-b, each a cost window plus a PNG. `ls_box=0`, `ls_samples=0`, `ls_shadowed=3` (Ultra's Full) throughout.

## Gates (read the deciding numbers in the log)
- Energy repeat (added 2026-11-01): shot-k1-b, shot-off-c, shot-k1-c, shot-off-d after OFF-b (alternated, 8 s each).
- Valid run: all expect lines, no forbid line (`LS_K pipeline UNAVAILABLE` included), RR create Success, every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`, `TLAS ... none 0`; `ls exact-k: active (ls_k 1 ...)` and `inactive` logged, and an `ls:` GPU line with `exact-k 2 + 1` (the active line logs only on the off/on transition, not on 1 -> 2).
- Participation (precheck): `over the cap (k-hit)` share and `remainder ... lights/px` reported; `traced lights (ls_k=0 would trace)` is the per-pixel light count the option can save on.
- Energy (unbiased estimator, rule changed 2026-11-01): the float mean luminance in `dumps.json` of the scene colour before post (pass fs `5c3fdabc477f316a`, 2560x1067 R11G11B10F, RR's input; `0.2126 r + 0.7152 g + 0.0722 b` of its `mean`), each k shot against the mean of its two neighbouring OFF shots, within the spread of the four OFF shots. Never the PNG previews (Reinhard + gamma, point-sampled: concave) and never the other R11G11B10F dumps p1-p9 (the game's bloom pyramid: its prefilter `3434e561` keeps the min of 4 taps, so more per-pixel noise reads darker at the same energy; TRACK_D_LOCAL_LIGHTS 9.11 "Live S7 inn"). The bloom ratio is reported, not a gate. No new `hard_dark` or firefly region against OFF (QA detectors as in lean_regression; maxima within 2x of OFF's, the 9.11 rule).
- Cost: `ls: local-light shadow pass GPU median ... exact-k K + 1 (variant 1)` of k1 and k2 against the mean of the OFF-a and OFF-b lines (`variant 0`); a k is kept only if its saving exceeds the OFF-a/OFF-b spread. Also `rt passes: GPU cost` and the frame time per window. Quality before frame rate: an equal-cost or noisier result stays off.

Duration: plan 147 s; one launch, about 4.5 min (at most 7.5 min of timeout). Disk: 4 PNG traces at about 60 MB plus logs, about 0.25 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `lsk` and receipt `ls-exact-k.json` (test-main must be the commit under test: a core-only change, no layer restart beyond the launch).

Offline check: `powershell -File tools\live_batches\ls_exact_k\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01 (after the gate change): every job and plan check passes; the four simulated-run checks (run A / run B, fake game) fail, identically with the job file before the change (a dry-run harness issue for a one-job batch, not the job).

## Result 2026-11-01 06:26 (main 392249e, INVALID only on the old `ls_k 2` active expect line, fixed)

- ls GPU median: OFF-a 0.722, k=1 0.556 (-23%), k=2 0.655 (-9%), OFF-b 0.720 ms. Saving >> OFF spread (0.002 ms).
- k-hit 78.6% of px (valid precheck). Final image mean (p14 RGB10A2): 58.51 / 58.39 / 58.43 / 58.40: within OFF-a/OFF-b.
- Local-light buffer (p6-p9 R11G11B10F): k=1 3% below OFF-b, OFF-a to OFF-b drift also 3% (flickering candles).
  Energy not decided: needs a repeat with OFF/k1 alternated twice before ls_k goes into any preset.

## Result 2026-11-01 07:13 (log `lsk1-exact-k-inn-s7-fg2.1.071324.log`, with the alternated repeat)

- ls GPU median: OFF 0.717, k1 0.555 (-23%), k2 0.654 (-9%) ms, very stable.
- PNG previews p8 / p9: k1 3-4% below OFF in every alternation (p9 OFF 4.99 / 4.92 / 5.00 / 4.84, k1 4.75 / 4.84 / 4.76, k2 4.74). These are bloom dumps, see the gate above.
- Float means (`dumps.json`), k / mean of the neighbouring OFFs, three alternations:
  scene colour `5c3fdabc` k1 -0.38 / -0.34 / +0.12%, k2 +0.04%; OFF spread 0.89%: **energy equal**.
  Bloom prefilter `3434e561` k1 -3.6 / -3.9 / -0.8%, k2 -1.9%: the min-of-4 filter on more noise.
  Final image p14: unchanged (bloom is about 1.8% of it).
- Verdict: unbiased (also GPU fixture 8e and `ls_exact_k_test` with occluded lights), -23% at k1. Off by default until the preset decision.
- The next run must show: the scene-colour float mean of every k1 shot within the four-OFF spread against its neighbours (as above); the bloom may stay lower.
