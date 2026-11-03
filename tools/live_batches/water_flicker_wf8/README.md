# wf8: reflection-specific S8 harbour attribution

Status: prepared offline, no live authorization, no run, no quality verdict. This folder is separate from the old
`water_flicker` batch so wf7 is never included accidentally. `predicted.json` was written before the job.

One `Water Queens House` launch, `visible_unfocused_fg`, 24 phases / 185 s plus load. Same wf7 camera protocol:
return -60 px/s for 4 s, turn +60 px/s from phase 1 to 5 s, request captures at phase start with 2000 ms wall-clock
delay. The static calibration moves -180 px first, matching the turn's central capture pose, then returns.
All compared windows disable water sun/local shadows, refraction, game shadows and glint AA **at startup**; the
record stride is 32 B. `rt_water_compose` alone is toggled. Other Ultra RT settings remain on. Outer queue restores
INI after the run; the final phase stops captures, releases exposure and leaves the isolated water setup.

| Data | Serial / windows | Purpose |
|---|---|---|
| Raw static diagnostic, 8 rendered frames | 98100 `diag-view-static` | Native-encoding and static-noise calibration |
| Raw compose diagnostic, 8 frames each | 98101 / 98103 `diag-view-delta-a/b` | Reflection-only signed relative delta; tier 1 vs unresolved |
| Raw game water, 8 frames each | 98102 / 98104 `snap-off-a/b` | Compose-off water-output proxy, including native glints |
| Presented beauty, 16 frames each | `snap-on-a/off-a/rroff/on-b/off-b` | Reflection-region display comparison, RR attribution |
| Trace, one PNG set | `diag-view-mask-rot` | Whole-water secondary control only |
| No captures, 20 s each | `cost-on/off` | Existing GPU-ms lines; every-window scene/capacity gates |

Raw streams: `water_game` before compose, `water_compose_debug` immediately after debug 25, `water_coverage`
from this frame's compose record markers. Coverage is mandatory: a black diagnostic pixel can be tier 1 with zero
delta. Debug 25 combines tiers 2-6, so report `1 -> unresolved` and `unresolved -> 1`; never infer hidden/off-screen/
sky subtypes from `water_reflect`'s different calculation. Its signed scalar is `(composed - probe-only) / luma(C)`.

After a separately authorized run, check each of the five beauty directories has exactly `p00.png` through
`p15.png` before scoring. A completion line alone does not guarantee asynchronous PNG writes succeeded; the job
forbids `PNG write failed` and capture-writer backlog. For each directory:

```powershell
$beauty = '<one beauty present directory>'
$expected = @(0..15 | ForEach-Object { 'p{0:00}.png' -f $_ })
$actual = @(Get-ChildItem -LiteralPath $beauty -Filter 'p*.png' | ForEach-Object Name | Sort-Object)
if (Compare-Object $expected $actual) { throw 'INVALID: missing or extra presented PNGs' }
```

Analysis after that check (replace paths with the five emitted burst directories):

```powershell
python -m scripts.qa.reflection_toggle <98101-dir> --calibration <98100-dir> --signal compose --mask-out reflection-a.png --out delta-a.json
python -m scripts.qa.reflection_toggle <98103-dir> --calibration <98100-dir> --signal compose --mask-out reflection-b.png --out delta-b.json
python -m scripts.qa.reflection_toggle <98102-dir> --calibration <98100-dir> --signal game --out game-a.json
python -m scripts.qa.reflection_toggle <98104-dir> --calibration <98100-dir> --signal game --out game-b.json
python -m scripts.qa.fg_present_flicker <the-five-beauty-present-dirs> --mask reflection-a.png --out presented-reflections.json
```

Use the same reflection-support union mask for every beauty window, and repeat with `reflection-b.png` to expose
mask sensitivity. Eight rendered frames correspond to sixteen FG presents approximately; callback latency and water
motion limit registration. Do not subtract pixels across turns. Compare repeat spread within this launch. The
whole-water rt_debug 21 mask is a secondary control, never the deciding reflection metric. The presented second
difference is supplementary; exact on/off counts come from the raw compose diagnostic metric.

Candidate outcomes and limits are in `predicted.json`. Native output cannot isolate SSR from animated normals,
probes, refraction or glints. RR-off supports RR involvement only; it also changes reconstruction. No cheap proven
stabilizer exists. Tier hysteresis needs shader radiance/tier history plus reprojection/disocclusion validation and an
off-by-default specialization constant; it is described but not implemented. Earlier edge fade worsened the broad
metric; RR water-vector/hit-distance keys were null. They stay off. No shader changes or DXC runs.

Budget: five 8-frame selective bursts = 296 B/render pixel, bounded by the runtime 3,686,400-pixel guard; eighty
half-display PNGs at <=1920x1080 RGB plus 1% container allowance, one trace and logs = **1,663,815,040 B** worst
case. Display <=3840x2160 is required for that presented bound. The wrapper checks the exact count and derived
budget before the common fake dryrun; the common legacy estimate alone does not count selective bursts.

Offline only: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/water_flicker_wf8/dryrun.ps1`.
The wrapper fabricates authorization receipts only inside temporary `.queue-fake-root` folders. They authorize no
real game run. Keep this batch outside all watched `captures/queue/pending` folders.
