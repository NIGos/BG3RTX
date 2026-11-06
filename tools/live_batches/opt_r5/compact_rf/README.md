# Compact RF probe state receipt

PREPARED_ONLY / NOT_QUALIFIED. Statistical offline replay gate failed; no staging or launch authorized.
Inn pack 61 has zero differences in 40 balanced adjacent OFF/OFF controls, while
4/40 candidate pairs differ (up to 63 specular pixels and 9 hit-distance pixels).
Two scenes: inn (`The Blushing Mermaid - 96h 39m`) and S8 (`Water Queens House`).
Each has static OFF/ON/OFF/ON windows of 600 rendered frames, reset-aligned to
120-frame GPU periods, and fresh QUIET after each acknowledged toggle.
Warmup exercises ON explicitly; specialization 1031 is not prewarmed by experimental_specs.

KEY: `rf_compact_probe_state`, raw developer setting, default OFF, no knob. Keep dev=1
as in compact_hit. Start from the same frozen Ultra release observation profile;
keep `rf_hit_shade=0` and `rf_hit_gi=0` throughout to exercise default reflections.
ReShade stays disabled (`keep_reshade=false`).
Hold exposure during the comparison and restore exposure hold and the candidate to OFF.
No other rendering feature is toggled between measured windows.

Decision pass: reflections (`rf: reflection pass GPU median`). The analyzer stores
all window medians, OFF repeat spread, and each ON minus its preceding OFF with a
pair verdict. Global SAVING requires every ON to beat every OFF by more than the
OFF spread. NO_SAVING succeeds as a completed measurement; missing receipts are INVALID.
Prior rf-opt2 exploratory paired replay: -0.126 ms. Current paired replay:
-0.122904 ms, CI [-0.157664, +0.019344] ms; noisy. Registers 128 -> 96.
Live cost and energy remain pending. See `docs/progress/rf-default.md` for offline gates.

Energy reuses scene_holds' trace export pattern and release_qual's `energy` reader:
one trace after each cost window (four exports per scene), float scene C before
post from rrpost slot 2 in dumps.json, RGB plus luminance. No PNG/raw images or bursts.
The separate 8-second export phases keep trace stalls outside GPU cost windows.
Two OFF replicates estimate noise (Student df=1); eight two-sided pair/channel checks
share the existing family alpha. Energy changes return ENERGY_CHANGE and nonzero;
missing, duplicate, unacknowledged, nonfinite or dimension-mismatched receipts are INVALID.
This is a float mean check, not a live image bit-equality test.

Generate: `python tools/live_batches/opt_r5/compact_rf/make_job.py`.
Default copies compact_hit's frozen exported Ultra profile; optional `--profile-exe`
uses a rebuilt release_qual/profile.cpp exporter. Never executes a game or runner.
Lint: `python tools/live_batches/live_flow.py tools/live_batches/opt_r5/compact_rf --frozen --report tools/live_batches/opt_r5/compact_rf/lint.json`.
Test: `python tests/compact_rf_jobs_test.py` (registered in the single CMake Python foreach).
Snapshot extension adds only these jobs' missing readiness queries; no corpus refresh.

Including 120 s load, nominal estimates are inn 262.3 s and S8 259.3 s, plus conditional
readiness. Frozen historical p10 estimates are in predicted.json; inn may exceed the
300 s hard stop. Readiness remains unproven; an incomplete capped run is INVALID.
No builds, shader compiles or GPU tests during live cost windows. Judge GPU ms, not FG fps.
