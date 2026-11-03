# vram_guard: vg3 iteration 4 (prepared, not queued)

One launch at Water Queens House, custom Ultra + FG 2, visible_unfocused_fg.
The driver supplies the real VRAM budget. `rt_vram_guard_budget_mb=0` explicitly
disables any stale inherited INI simulation; no positive override is permitted.
ON enables guard, compact and elastic; OFF/restore disable all three. Keys remain
experimental and off by default. A layer rebuild and restart are required.
This preparation does not authorize a live run.

`predicted.json` was updated before vg3 job/plan generation. Generate with
`python tools/live_batches/vram_guard/make_job.py`. The active job is vg3 only;
vg2's old job and generator are retained in `archive/` for reference. The old
plan remains for historical analysis. Do not run the archived generator.

The plan has 18 phases, 256 s plus load, three PNG traces and about 190 MB.
After warmup and the same 10-notch camera lowering as vg2, it performs identical
20 s right/left turn pairs at 60 pixels/s in OFF-a, ON, OFF-b order. Each pair
has a 10 s static settling window. Opposite turns return the camera orientation.
Static energy OFF/ON/OFF shots follow, then 25 s settling and 20 s measured ON
recovery. Exposure stays held from before the first turn through recovery.
Restore releases exposure and disables the bundle.

The mandatory `post_gate` runs `gate.py`. Per direction and metric, it compares
turn-wide peak and mean of complete 120-frame capture summaries. Intervals
crossing settings/phase boundaries are discarded. Counts must match and be at
least two per window. Missing or unequal evidence fails. For each statistic,
the tolerance is absolute OFF-a minus OFF-b plus 0.02 draws/frame (two rounded
hundredths). ON must stay below the larger OFF statistic plus that tolerance.
Opaque drops and alpha made opaque are both gated. Peak catches a bad summary;
mean catches more drops below an unchanged peak. No ordinal camera-angle
alignment is claimed: log emission cadence can shift within a turn. Input paths
and direction match; geometry/animation identity still needs inspection.

Every ON memory snapshot (periodic, refusal, state change) after the derived 1 s
in-flight setting transition must be within budget with consumer accounting. The
post report `<log>.vg3.json` retains ON consumer snapshots, BLAS reserved/live/
pending lines, comparison series/tolerances, float energy and failures. Repack
or refusal is not mandatory when real headroom never triggers one. Existing
logs retain refused growth attempts. Report GPU ms from existing RT/GI cost
lines, not unfocused frame time; the bundle has no dedicated GPU timer.

Float energy uses the existing production scene-colour reader (FS 5c3fdabc,
slot 0 in dumps.json), with pre-exposure normalization. Require zero nonfinite
scene pixels, exposure probes, and ON within +/-0.92% of mean OFF. The report
also includes current OFF-OFF spread. PNG/bloom energy is never used.

Offline validation: `powershell -NoProfile -ExecutionPolicy Bypass -File
tools/live_batches/vram_guard/dryrun.ps1 -Force`. Scratch must be outside every
checkout. This tests wiring and failure propagation, not live quality.
