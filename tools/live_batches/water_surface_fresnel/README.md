# Water coverage / interface / column split (M5.6 step 2)

Prepared only. Lives paused; nothing submitted to a watched inbox.

One real-Ultra S8 Water Queens House launch, visible_unfocused_fg. Provision the default-off
key at startup; toggle 0/1/0/1 within the same session. Four static 12.0 s pool-rim windows
retain two post-water float shots each, followed by four 12 s held E/Q cost windows.
128.2 s scheduled plus load, about 665 MB with bounded float previews. Static minimum >11.864 s derives from 120 frames
at the measured S8 27.076 fps, one discarded straddling interval, 1 s settings latency
and two 1 s trace stalls. Regenerate with `python -m tools.live_batches.water_surface_fresnel.make_job`;
the shared helper also adds producer readiness and sizes restoration to 1.1 s.

Every ON window reads the coverage-weighted ownership, invalid and energy-partition line;
zero/partial/full histogram and source/spec counters remain available. The post gate reads
all eight full-resolution RR colour float means and lighting exposure, plus explicit water-capture-fix
post_core paired _p.f32 previews. dump_preview_f32=1 warms up without captures, then stays on for four static shot windows; dump_raw stays 0.
The fixed 1280 x 533 preview supplies half-open pool-rim [490,75,825,285] and open-water
[0,75,245,340] ROIs (70,350 and 64,925 samples). Missing pairing/files, wrong size,
nonfinite pixels and out-of-bounds regions invalidate the post gate. Two previews per shot add
166.56 MiB for eight shots; raw attachments would add 5.86 GB. Brightness may change; image quality needs review against predicted.json.
Native foam retains coverage c; only the (1-c) clean contribution receives RT replacements.
Every ON window requires surface FS GPU ms separately from compose/copy/dispatch. The timer
covers candidate patched draws, including vertex/raster work. OFF remains untimed: report ON
absolute FS cost, never an inferred FS delta. Only the first submission of a recorded pass
is sampled; slots remain reserved until command reset (64 passes x 256 draw pairs).
Every ON interval requires nonzero frames/passes/draws and dropped 0. Prior static compose delta is +0.0255 ms;
regional energy and FS ms remain unmeasured until the next authorized run.
Coverage histogram bins use ceil(255c); bin 255 includes near-full coverage. Radiance uses float c. Last phase restores key 0 and releases exposure hold.

Compose aborts use the maximum share from VALID compose intervals with invalid 0;
the current bound, sample count and witness are regenerated in predicted.json.
The half-ray subtraction bounds rounding of the producer's %.0f rays/frame counter
(core/water.cpp); there is no extra tolerance.
Every selected interval must meet aborted / (frames x (rays/frame - 0.5)) <= bound;
invalid still requires exactly zero. Missing/zero denominators fail closed.
The stopped wsf1's one abort has share 3.559e-8. See
[partial result](RESULT_wsf1_031444.md); this does not qualify the feature.

Offline checks: `python tools/live_batches/live_flow.py tools/live_batches/water_surface_fresnel`
and `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/water_surface_fresnel/dryrun.ps1`.
Dryrun uses scratch fake roots only. Material proof: [evidence](../../../docs/evidence/water-foam-coverage.md).
