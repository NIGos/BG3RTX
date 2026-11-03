# Water hit cost draft

No game launch or queue submission. One S8 Water Queens House launch, Ultra,
visible_unfocused_fg, fixed rt_water_hit_shade=2 and rt_water_compose_shift=1.
The four groups independently compare rt_water_hit_cost 0/1/0/1 (sparse),
0/2/0/2 (cone/zero), 0/4/0/4 (candidate order), and 0/7/0/7 (combined).
All values 0/1/2/4/7 warm before the first capture. Each group takes four static float-energy windows
(two captures each), then four static diagnostic-free cost windows (14 s each; exclusive lint minimum > 13 s).
This isolates hit cost from camera motion and the pending compose-shift issue.
Energy uses water-compose float pre-post scene plus same-frame delta and exposure,
with separate OFF-repeat/encoding bands per group. Every ON must preserve energy,
coverage, zero invalid hits and zero world truncation. No radiance gain correction.

Judge cost ONLY on [water] reflections GPU median and p90; report compose GPU
separately. Discard the first GPU report after each toggle; use the last complete
120-sample report within the cost window (two reports required). Do not use frame
time, total RT time or traced capture windows for cost.
The goal is lower than mode 2's supplied 0.8-2.4 ms median / up to 3.3 ms p90.
Budgets are optimization targets, never hard stops. Report all measured numbers.
Existing m4 energy post gate checks every ON; cost reporting is separate (no budget gate).
Mode 1 shares cone/zero culling, but sparse/order only affect out-of-grid mode 2;
mode 1 remains covered by offline specialization checks, not claimed live-qualified.

557 s phases + loading (11.3 min dryrun estimate), 32 PNG traces, no float bursts,
1930 MB conservative estimate. Final restore clears experimental keys/exposure;
runner restores per-run overrides. Run dryrun.ps1 for synthetic queue verification.
The synthetic receipt grants no live authorization. Commit predicted.json through
the coordinator before an authorized run (Git metadata is read only here).

Lint assumptions: 30 rendered FPS, 1 s settings latency, 4 s per trace stall.
Energy shots last 18 s (exclusive minimum > 17 s); cost windows last 14 s
(two retained GPU reports after the skipped first line). Load-time roof overflow
and mesh-pool warnings are rejected with zero tolerance in measured windows.
Regenerate with `python tools/live_batches/water_offscreen/make_jobs.py`; canonical JSON is the seed.
The 18:56 shots had no complete periodic lines in several windows: two trace
stalls consume the cadence budget. All existing every-window gates remain; the
longer shots reserve 8 s for trace stalls plus apply and two 120-frame periods.
