# SHaRC: offline PT decision plan

No live job is prepared or authorized. Keep `sharc=0` until this gate passes.
The old `sharc_far` +1.5% energy prediction is not evidence of PT superiority.
A brighter cache or a lower noisy residual alone does not qualify reconstruction.

1. Inventory retained replay packs using `tools/frame_replay.md` and
   `tools/live_batches/replay_capture/README.md`. Require identical camera,
   G-buffer, TLAS including cut roofs/casters, qualified hit materials, sun/cloud
   and native ambient inputs. Record pack manifest/hash and omitted descriptors.
   S8 is the first cohort; add camp only if a retained complete pack exists.
   Missing pack data is an offline blocker, never a request for a new live run.
2. Verify the existing production replay first:
   `bin/frame_replay.exe --pack <pack> --out <out>/baseline --repeat 32`.
   Inspect bit-equal share and mean/max difference against the captured giRaw,
   guard counters and material coverage. Run `--self-test <scratch>` and
   `--pt-self-test <scratch>` before interpreting reference results.
3. Produce independent references with existing commands:
   `bin/frame_replay.exe --pack <pack> --pt-reference --pt-iters 1024 --out <out>/pt-a`
   and another equal-length seed/reference replicate if supported by the pack
   and harness. Increase iterations until per-tile reference uncertainty is
   smaller than the candidate effect; retain `pt_report.txt`, `pt_tiles.csv`,
   `pt_total.pfm`, `pt_live_domain.pfm` and one fixed-exposure comparison image.
   Use `--native-ambient --pt-iters 1024` for the far-field/environment split.
   Declare calibration, unsupported lights/materials and caster policy exactly
   as documented in `tools/frame_replay.md`; do not compare unlike radiance domains.
4. Candidate input must be an already retained, provenance-matched SHaRC OFF/ON
   float replay/output sequence, including injection, resolve, reset and history.
   `frame_replay` currently replays gi_hit_direct and the PT; it does NOT replay
   SHaRC injection/resolve history. Merely setting `--define SHARC=1` cannot
   manufacture that comparison. If no matched candidate sequence exists, record
   `REJECT_FOR_RELEASE: offline history comparison unavailable`, with the pack
   inventory counts. Do not revive the old live job or build a new framework.
5. If candidate inputs exist, use existing `tools.m4_promotion_gate.Run`
   scene-colour/float readers for exposure/provenance checks, and existing QA
   temporal detectors (`scripts/qa`, demonstrated by `sharc_far/rays_flicker.py`)
   on the retained static/moving sequences. Report PT-normalized mean signed
   error, absolute/RMS error by 16 x 9 tile, leak crops behind cut roofs, cold
   history, settled history, disocclusion and reset recovery. Float pre-post
   energy is the measurement; PNGs are fixed-exposure inspection only.

Deciding observable: SHaRC error against the same PT radiance domain decreases
beyond the PT/OFF-repeat uncertainty in every supported cohort and history state,
without signed mean drift, wall leaks or slower recovery. Derive limits from
reference convergence and OFF repeats, never from the ON candidate. Report the
existing trace/resolve/inject GPU timers when available; do not infer live cost
from offline dispatch or compare screenshots from different sessions.

Promote only with matched complete inputs, a resolved PT improvement and all
energy/history/leak gates passing. Reject for release if an actual gate fails,
improvement is unresolved, or the existing tools/retained inputs cannot establish
the comparison. List counts, uncertainties and the exact missing input in the
result. This plan itself supplies no rendering qualification or numerical pass.
