# Native DLSS 5: READY_FOR_LIVE preparation

One S8 Water Queens House Ultra / FG 2 candidate, prepared only. Nothing was
queued, launched, or copied into a watched inbox. Fresh live authorization is required.
Known fixture internal-image validation failure remains unresolved; this is not a
quality or validation pass. Runtime support is signed 310.8.0; custom 310.8.3 is
accepted by the app but rejected by NGX core.

`jobs/dlss5-s8-ultra-native.json` uses `plans/dlss5_s8.json` directly. The launcher
must first stage the chosen user runtime through its normal startup path. The
queue job does not run the launcher or copy binaries. Keep both files together
when the coordinator explicitly authorizes the serial runner; never copy now.

Cold `dlss5_nr=1` provisions NR; `dlss5_nr_bypass` changes live for four static
600 measured-frame OFF/ON/OFF/ON windows (runner budget 630 including 30 settling). ON prefetch happens before measurements. Stats
reset and 30 settling frames isolate every window. Separate OFF/ON PNG traces
follow the cost windows. Final bypass is 1; runner restores original settings.

Every ON window must contain NR GPU ms; every cost window must preserve FG 2.00,
scene/capture capacity and no device loss/NR failure. Inspect the PNG pair for
faces/details, HDR lighting, water and UI. Do not infer linear HDR energy from PNGs.
`predicted.json` retains the fixture cost prior and its limits.

Lint (2026-11-20, claude/r2-tools): `py -3 tools/live_batches/live_flow.py --frozen tools/live_batches_staged/dlss5`
and the live form both report 1 job, 0 failing. Changes against the untracked 2026-11-05 copy: the two PNG windows
are 10.5 s (8.1 s guaranteed 0 complete traces: the trace at 5 s plus the 3.4 s worst capture stall and the settings
latency need more than 10.27 s), and the job's readiness queries (6) and its NR GPU expect pattern are appended to
`tools/live_batches/lint_corpus_snapshot.json` (append only, unproven until a VALID run). The cost windows keep
their 630 runner frames (30 settling + 600 measured); the runner ends them on frame count. Conditional readiness
stays unproven: no NR live evidence exists. This is not a live qualification. Committed on claude/r2-tools; nothing
was queued or launched. The perf_ab batch's `perf-ab-dlss5-s8` is the log-only cost A/B; this job adds the PNG pair.
