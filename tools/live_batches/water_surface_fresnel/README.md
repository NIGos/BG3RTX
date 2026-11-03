# Water coverage / interface / column split (M5.6 step 2)

Prepared only. Lives paused; nothing submitted to a watched inbox.

One real-Ultra S8 Water Queens House launch, visible_unfocused_fg. Provision the default-off
key at startup; toggle 0/1/0/1 within the same session. Four static 11.9 s pool-rim windows
retain two post-water float shots each, followed by four 12 s held E/Q cost windows.
127.8 s scheduled plus load, about 490 MB. Static minimum >11.864 s derives from 120 frames
at the measured S8 27.076 fps, one discarded straddling interval, 1 s settings latency
and two 1 s trace stalls. Regenerate with `python -m tools.live_batches.water_surface_fresnel.make_job`;
the shared helper also adds producer readiness and sizes restoration to 1.1 s.

Every ON window reads the coverage-weighted ownership, invalid and energy-partition line;
zero/partial/full histogram and source/spec counters remain available. The post gate reads
all eight full-resolution RR colour float means and lighting exposure, rejecting missing or
nonfinite evidence. Brightness may change; image quality needs review against predicted.json.
Native foam retains coverage c; only the (1-c) clean contribution receives RT replacements.
Actual FS plus compose cost is unmeasured: ON evaluates native and clean shading separately.
Compose GPU timing excludes the extra forward FS evaluation; report its measurement limit.
Coverage histogram bins use ceil(255c); bin 255 includes near-full coverage. Radiance uses float c. Last phase restores key 0 and releases exposure hold.

Offline checks: `python tools/live_batches/live_flow.py tools/live_batches/water_surface_fresnel`
and `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/water_surface_fresnel/dryrun.ps1`.
Dryrun uses scratch fake roots only. Material proof: [evidence](../../../docs/evidence/water-foam-coverage.md).
