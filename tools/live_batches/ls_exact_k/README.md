# ls_k live batch (live_batch ls-exact-k), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the game takes focus and the pointer: the user must be away), FG 2, the key toggled inside the run, 20 s diag-off `cost-*` windows, one PNG per 8 s `shot-*` phase, no float bursts. Receipt `ls-exact-k.json`; the plan goes to test-main `captures\live_batches\ls_exact_k\plans`.

1. `lsk1-exact-k-inn-s7-fg2` (The Blushing Mermaid - 96h 39m, S7 inn: candles and lamps, many local lights per pixel; 147 s). [TRACK_D_LOCAL_LIGHTS](../../../docs/TRACK_D_LOCAL_LIGHTS.md) 9.11 `ls_k` (re-added as the LS_K variant). Warm-up OFF, then `diag-precheck-k1` (15 s, `ls_k=1`, `ls_stats=1`): its `ls exact-k stats` line needs pixels over the cap (`k-hit` above 0.0%), so a view with at most one traced light per pixel is INVALID, never a pass. Then OFF-a / k1 / k2 / OFF-b, each a cost window plus a PNG. `ls_box=0`, `ls_samples=0`, `ls_shadowed=3` (Ultra's Full) throughout.

## Gates (read the deciding numbers in the log)
- Valid run: all expect lines, no forbid line (`LS_K pipeline UNAVAILABLE` included), RR create Success, every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`, `TLAS ... none 0`; `ls exact-k: active (ls_k 1 ...)`, `(ls_k 2 ...)` and `inactive` each logged.
- Participation (precheck): `over the cap (k-hit)` share and `remainder ... lights/px` reported; `traced lights (ls_k=0 would trace)` is the per-pixel light count the option can save on.
- Energy (unbiased estimator): mean PNG luminance of `shot-k1` and `shot-k2` minus `shot-off-a` within |`shot-off-b` - `shot-off-a`|; no new `hard_dark` or firefly region against OFF (QA detectors as in lean_regression; maxima within 2x of OFF's, the 9.11 rule).
- Cost: `ls: local-light shadow pass GPU median ... exact-k K + 1 (variant 1)` of k1 and k2 against the mean of the OFF-a and OFF-b lines (`variant 0`); a k is kept only if its saving exceeds the OFF-a/OFF-b spread. Also `rt passes: GPU cost` and the frame time per window. Quality before frame rate: an equal-cost or noisier result stays off.

Duration: plan 147 s; one launch, about 4.5 min (at most 7.5 min of timeout). Disk: 4 PNG traces at about 60 MB plus logs, about 0.25 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `lsk` and receipt `ls-exact-k.json` (test-main must be the commit under test: a core-only change, no layer restart beyond the launch).

Offline check: `powershell -File tools\live_batches\ls_exact_k\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01: all checks passed.
