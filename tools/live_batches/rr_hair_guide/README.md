# ef2: pre-post RR hair resolve

Prepared only. No queue staging or launch script. This job uses Test Dialogue3,
one launch, the Ultra preset with native hair off, and no camera/dialogue input.
An authorized future run must show the ef1 white-haired character against the
DoF background. All settings reset per window; auto-run restores the INI on exit.

Order: baseline A / rr_post=0 / rr_hair_resolve=1 / baseline B. The documented
SDK cannot accept bare hair alpha as its transparency overlay; that guide is
unavailable, not silently substituted. See predicted.json for the hypothesis,
historical numbers, energy and grading criteria, and the coverage-proxy limit.

Each window settles for 5 s, records K8 for guide masks, separately records 16 presented
frames, captures one float-stat trace, then measures 10 s of clean GPU timing.
Warmup 40 s avoids the observed dialogue camera movement around 41 s after load.
Total 165 s scene plus 120 s load = 285 s; conservative disk estimate is 782 MB.
Timing windows contain no captures or diagnostics. Read the existing ngx/rrpost
GPU ms lines there; the first straddling sample is excluded. Other offline work
must be idle during those windows. Capture readback itself cannot qualify cost.
The candidate timing window must also log current class-5 occupancy and mixed
4x4 resolve activation. A fallback cannot qualify as a tested candidate. The
occupancy-capture dispatch precedes the existing evaluate timer; chain timing
includes resolve but does not measure the candidate's total GPU cost.

Energy checks compare baseline A/B against hair-resolve at both ungraded
stages: **Xr (RR colour: dither-resolved X)** and **Z (RR output before the
grade)**. The existing energy trace already captures both: locate the trace
pass tagged rrpost, where color slot 0 is Xr and slot 2 is Z, and join the
image/pass references to dumps.json. Read their full-resolution decoded float
RGB means, weighted luminance and nonfinite counts. Use pre-post compose pass
5c3fdabc mean to normalize scene drift; compose alone cannot detect darkening
introduced by the candidate. Preserving a Sharp cell mean does not establish
preserved Xr energy when DoF A/C varies across that cell.

Candidate Xr and Z mean ratios must stay within the baseline-repeat spread.
Reject nonfinite values or ambiguous scene changes. rr_post=0 is a temporal
reference only: its graded input/output is not comparable to linear Xr/Z.
After energy checks, compare baseline-versus-resolve grade constants/LUT
identity and float graded-output means. Neither bloom nor PNG/PQ means certify
energy. dump_raw=0 suffices: dumps.json means use decoded full-resolution
floats even though PNG width is 1024. One energy sample per baseline supplies
a repeat bound, not a confidence interval.

One standalone shaders/rr_hair.hlsl request is auto-discovered (+1 request);
shaders/variants.txt registry delta is 0.

Offline validation (never launches the game):

```powershell
python -m tools.live_batches.rr_hair_guide.make_job
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/rr_hair_guide/dryrun.ps1
```

After explicit live authorization, copy the plan to the job's exact
captures/live_batches/rr_hair_guide/plans/ef2.json path. Keep the job outside
every pending inbox until then. The fake dryrun uses marked scratch directories
outside checkouts; its fabricated receipt has no live authority.

After the authorized run, use its own logs:

```powershell
python tools/live_batches/edge_flicker/analyze.py --job tools/live_batches/rr_hair_guide/jobs/ef2-dialogue3-hair-guide.json --plan tools/live_batches/rr_hair_guide/plans/ef2.json --game-log RUN.bg3rtx.log --runner-log RUN.runner.log --out ef2-report.json
```

Inspect dialogue_roi.presented.hair_box and rigid_control metrics: residual,
sparkle share and PQ mean. Registration excludes the scaled subtitle region.
The analyzer remains an attribution report; separately read float energy and
GPU timing before any quality decision. No quality pass is claimed offline.
