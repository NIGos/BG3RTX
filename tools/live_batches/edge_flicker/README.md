# Character-edge flicker attribution (offline prepared)

`ef1-dialogue3-attribution` loads **Test Dialogue3**, the port save used by
`dialogue_shadows`. It sends no camera or dialogue input. The first shot must
match the white-haired character against the blurred background in
`edge_flicker_user.webp`. A camera cut invalidates the comparison; the analysis
reuses `dialogue_shadows.pair_registration.registration` and never warps beauty.
No save is changed. No job is staged in a watched queue by these tools.

`predicted.json` was written before the implementation and contains the four
candidate outcomes. One launch, baseline A / independent toggles / baseline B:

| Window | Change from Ultra baseline | Meaning |
|---|---|---|
| fg-off | `fg=0` | Native versus displayed-frame attribution |
| ao-off | `gi_ao=0` | RT character self-occlusion contribution |
| sun-off | `rt_shadows=0` | RT sun contribution, global switch |
| gi-off | `gi_strength=0` | GI image contribution; AO kept at baseline |
| rr-off | `rr=0` | Game SR evaluate; also changes RT denoising paths |
| cbt-off | `rr_cbt=0` | Remove colour-before-transparency guide |
| post-late | `rr_post=0` | RR after the game's post chain; order probe |

Each window resets all these keys, settles for 5 s, records **8 consecutive
rendered frames**, then separately records **16 presented frames**, including
generated frames with FG on. Separate captures avoid the rendered burst's
end-of-burst GPU wait landing in the presented window. There is a 40 s initial
warmup and a final restore phase; auto-run restores the INI on exit/failure.
The first burst starts after 45 s: dialogue_shadows records camera movement as
late as 41 s after load. The redundant SSS probe is omitted to keep five minutes.
Both domains use nearest half-size pixels. Frame counts are exact; the existing
motion runner's phase/settle bounds use seconds. FG-off presents span twice as
many native frames as FG-on presents. Rates and parity are supplementary evidence;
there is no asserted native/generated endpoint identity or equal-time sampling.

Requirements: a **diagnostic host built with this change**, HDR10 RGB10A2
presentation (format 64), render <=2560x1067 and output <=3840x1600. Both
capture paths refuse larger extents. `pan_edge=1` extends the existing recorder,
with one omitted warmup frame, K8 depth/final, and material/albedo at frames 1/8.
The transport checks all 20 samples; no partial burst is accepted. It stores raw
packed pixels on the half-size grid, preserving material class bits and depth.
The diagnostic key defaults to 0; ABI and shader requests are unchanged (+0).
The normal rendering path gains no GPU pass. Capture cost is unmeasured and
these windows cannot qualify performance. The host must be restarted for this DLL.

Native hair stays at its default 0. It requires startup capture/stride/origin
setup and has a known Dialogue3 pool overflow (HANDOFF/PROGRESS); toggling the
key alone cannot provide a valid comparison. There is no character-only sun or
DoF-disable key here. `rr_guides_at=1` and `rr_guides_v2=1` stay at their defaults:
late guide reads can see reused G-buffer storage, not a corrected hair guide.
CBT is the independent supported guide switch used here; SSS stays at default 1.
See `layer/ngx.cpp` (`record_guides_at_gbuffer`, `rr_evaluate`, `on_evaluate`),
`core/gi.cpp` (`rr_active` selects RT denoising), and `shaders/gi/gi_common.hlsli`
(RT2 alpha classes 3 skin / 5 hair). `rr_post=0` changes more than DoF; neither
that probe nor RR-off proves a single candidate by itself.

Offline validation (never launches the game):

```powershell
python tools/live_batches/edge_flicker/make_job.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/edge_flicker/dryrun.ps1
python tests/edge_flicker_test.py
```

The common fake queue checks INVALID with expectations present and success with
expectations stripped, solely in `.queue-fake-root` scratch directories. It does
not grant permission for a live run. `visible_unfocused_fg` preserves FG while
using posted input; a covered/minimized game is rejected by the existing runner.
Future authorized staging must copy the plan to the job's exact
`captures/live_batches/edge_flicker/plans/ef1.json` path. Keep jobs outside pending
until the user explicitly steers this batch. No staging/launch script is provided.

After an authorized run, use that run's game and runner logs (not global logs):

```powershell
python tools/live_batches/edge_flicker/analyze.py --game-log RUN.bg3rtx.log --runner-log RUN.runner.log --out edge-report.json
```

`--capture-root DIRECTORY` remaps captured directory basenames after relocation.
No manual mask painting or manifest is needed. The analyzer validates all queue
expectations and every window, obtains capture paths from their logged timestamps,
requires P00..P15 and a complete K8 transport, then checks each shot's first/last
albedo against baseline A. The fixed mask is derived from both baseline bursts:
depth steps touching class-3/5 support, union all class-5 hair. The control is
eroded skin support excluding hair and edges. Armour has no character class;
its silhouette cannot be attributed with this mask. It reports per-pixel temporal
variance averaged over each mask, depth variance, albedo-motion fraction, midpoint
residual by unlabeled parity, and the reused `scripts.qa.detectors.temporal` test
against **both** baselines. Thin supports unable to hold the QA detector's
81-pixel footprint remain INCONCLUSIVE. The unmasked edge metric still describes
the shimmer: masking every changing hair pixel would discard the observable.

The report ranks reductions separately for rendered/presented frames. A candidate
must beat the lower baseline by more than the baseline repeat spread. This is
descriptive attribution, not a confidence interval or causal proof: animation,
different base FPS and synchronous presented readback can confound it (TRACK_B_FG).
PQ code means are not float pre-post energy; no quality or energy-preservation
pass is issued. Inspect the shot, motion fraction, rate evidence and interior
control before interpreting the largest reduction. Half-size sampling can miss
one-pixel strands, so a null result cannot exclude a candidate. The current
depth metric is post-lighting depth, not a captured DoF CoC; DoF remains a proxy.

Budget: 9 x K8 raw + 144 half-output RGB PNGs with 1% container allowance,
plus 70 MB logs/summaries = **1,477,283,200 bytes** at the enforced extent limits.
No full trace dumps or input-copy bursts. Scene phases **180 s**, with 120 s load
allowance **300 s / 5 min**; timeout 420 s is a failure ceiling, not the target.
Keep raw captures until analysis/review; then remove only those explicitly
identified burst directories, retaining the report, logs and predictions.
