# ef3: static S8 RR post-order decision

Retired 2026-11-04: rr_hair_resolve removed; use post_temporal for the retained post investigation. Do not run the preserved jobs; the generator stops before writing files.

Historical: superseded by `post_temporal`. The user rejected `rr_hair_resolve`; this attribution job must not enter the next live series. Its original job, gates and predictions remain as history.

Prepared outside every watched inbox. No game launch or queue staging is part
of this task. `predicted.json` was written before the job and plan.

Scene: **Water Queens House (S8), ordinary gameplay, companion close to the
camera, camera held still**. Never use a dialogue. No Q/E, mouse turn, or other
camera input is in this plan. This checkout cannot establish the saved camera
framing offline: before any future live authorization, the batch owner must
verify a static shot with hair and a rigid wall/armour patch in the configured
boxes. If the loaded scene does not supply that shot, prepare a matching static
scene and update the boxes before running. The job is a prepared candidate,
not evidence that the current save already has the required framing.

Proposed boxes use extent 1920 x 800: hair (800, 80)-(1100, 380), rigid control
(1150, 300)-(1450, 550). These are proposed S8 coordinates, not copied Dialogue3
coordinates. Confirm both boxes on the first capture; reject a mismatched box.
`analysis.rois`, `analysis.roi_extent`, and `analysis.subtitle_top=800` carry
the framing contract to the analyzer. Whole static scene is used for alignment.

One run: rr_post **1 / 0 / 1 / 0**, labels base-a / post-late-a / base-b /
post-late-b. Every window has 5 s settle, K8 rendered burst, separate P16
presented burst, one float-stat trace, then 10 s clean GPU timing. Then resolve **0 / 1 / 0 / 1**, labels resolve-off-a / resolve-on-a /
resolve-off-b / resolve-on-b, with rr_post=1. Resolve remains a CANDIDATE, off
in presets: user liked normal-view A/B; 6x crop artifacts and drift-confounded
ef2 do not justify removal. Auto-run restores the original INI on exit. One future
launch, 285 s scene + 120 s load, estimated 1,554 MB plus at most 148 MB of native PNG payload. Captures never overlap timing.
Do not build or run diagnostics during live timing windows.

Analyze hair residual/sparkle and rigid control in **every** window. Integer
sub-window registration handles slow translation; rigid mismatch rejects cuts
and non-rigid camera changes. Each late window must beat both baseline repeats
beyond the measured same-state repeat spread while retaining detail/brightness.
Compare whole-frame PQ RGB histograms, means and highlight percentiles in
rendered/presented results. These describe appearance, not linear scene energy.
The traces also support native grade/LUT identity and same-stage float means
from dumps.json. Review DoF edges and bloom/highlight spread. Ungraded pre-post
RR output and late graded RR output are different stages: never equate them.
Read TRACK_A_RR.md for the earlier grading and timing tradeoff before deciding.
No offline quality pass or live cost is claimed.

Offline checks (no game):

```powershell
python -m tools.live_batches.rr_post_decision.make_job
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/rr_post_decision/dryrun.ps1
```

After a future authorized run with verified framing:

```powershell
python tools/live_batches/edge_flicker/analyze.py --job tools/live_batches/rr_post_decision/jobs/ef3-s8-post-decision.json --plan tools/live_batches/rr_post_decision/plans/ef3.json --game-log RUN.bg3rtx.log --runner-log RUN.runner.log --out ef3-report.json
```

Only after explicit authorization copy the plan to its configured
`captures/live_batches/rr_post_decision/plans/ef3.json` path and use the serial
queue. Dryrun receipts are fabricated inside marked scratch roots and have no
live authority.

The analyzer reads the configured S8 ROIs (no dialogue subtitle exclusion),
checks logged swapchain extent against each native ordinal-zero PNG, and writes report
artifacts beside the report JSON. `hair-native-side-by-side.png` and individual
`*-native.png` files preserve source pixels at 1:1. Ordinal zero from each P16
burst is used, with source present IDs recorded, never a selected best frame.
`hair-secondary-diff-6x.png` is the secondary panel. Inspect images at 100%:
a viewer fitting the wide panel to screen will shrink them. PNGs are 8-bit PQ,
not a replacement for HDR viewing or float energy. Histograms use 64 fixed RGB
bins over [0,1], whole-frame means and 95/99/99.9 percentiles in both domains.
Same-state repeat ranges cover residual, sparkle, mean, gradient and detail.

`fg_snap_full=1` opts into a native-size ordinal-zero PNG alongside compact P16; default is 0. It changes
only offline capture encoding, no render shader, ABI or shader request count.
Only the first present in each burst is additionally saved as `native.png`,
so eight native images add at most about 148 MB plus PNG overhead at 3840x1600. Live evidence and measured
GPU cost are still pending. No game launch is authorized by these files.
