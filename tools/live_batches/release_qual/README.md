# Final release qualification: READY_FOR_LIVE, paused

Base ab66c855, branch final-qual. No commit, launch, authorization or real queue staging.
The final generator reuses the rel1/rel3 generators and rel2 S8 same-ray OMM smoke phases.
Queue **only** the two files in `final/jobs`, never the three source receipts in `jobs`.

| Launch | Coverage | Expected with 120 s load | Disk estimate |
| --- | --- | --- | --- |
| final-s8-ultra-high | Ultra rel1 then High rel2 at Water Queens House | 3.72 minutes | 130 MB |
| final-stock-hair-dialogue1 | Ultra stock cards, native hair/scalp | 3.32 minutes | 70 MB |

Both launches have a 300-second hard timeout, including load, and max_minutes=5.
Cold or blocked readiness fails the receipt instead of extending the launch. The estimates
have unproven readiness transitions; they are planning numbers, not measured durations.
No builds or compiles during live cost windows. All input uses visible_unfocused_fg.
The High transition explicitly resets the full exported release profile (dev=0), acknowledges
changed settings/pipelines, and keeps OMM/masks/fine alpha LOD enabled. No experimental hair.

Five reset-aligned static/pan/rotation cost windows preserve the rel1 smoke gates for each S8
preset. One acknowledged stats period per window; no trace or expensive diagnostics there.
Ultra also exercises the master OFF/ON cycle. High alone runs the existing separate OMM
same-ray comparison: compared > 0, not compared 0, violations 0. Dialogue preserves all
native hair/scalp, capacity, scene wait, pipeline and GPU gates. All jobs forbid device loss,
crashes and active strand producers. The final S8 job uses combine_group and its fail-fast slices.

Three separate static energy exports (Ultra S8, High S8, Dialogue1) are automatically read by
`analyze_final.py` after the jobs. This produces `<log>.final.json`: per-pass GPU ms with signed
baseline deltas, plus float pre-post C luminance joined to rrpost slot 2 in dumps.json.
No raw floats, bursts, presented captures, shader dumps or redundant diagnostic captures.
The energy trace supplies the stock-hair visual check too. Inspect cutout silhouettes/holes,
Ultra water/edges, stock cards and face/neck shadows. Human review is still required.

`predicted_final.json` freezes all gates, reference per-window numbers, shader-fix predictions
and limitations. Reference costs come from rel1 20:58:53, rel3 21:03:04 and High rel2 12:13:05.
Some historical windows have no complete RT/GI timer sample; no different view is substituted.
Moving-view min/max values are observed ranges, not a statistical noise estimate. Record and
explain deviations; do not invent a percentage tolerance or judge occluded frame time.
Dialogue baseline C luminance is 0.3743881342 (one float sample, no baseline noise repeats).
Rel1/rel2 exported no float capture. Their historical energy delta is UNAVAILABLE, never PASS;
retain the new same-scene floats. No cross-run image A/B or energy-improvement claim is allowed.
A shader-change energy delta must be signed and physically explained, or supported by PT.

Regenerate (offline, from repository root):

```powershell
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 tools/live_batches/release_qual/profile.cpp -o "$env:TEMP/bg3rtx-final-profile.exe"
python tools/live_batches/release_qual/make_final.py --profile-exe "$env:TEMP/bg3rtx-final-profile.exe"
python tools/live_batches/live_flow.py tools/live_batches/release_qual --preflight --frozen --report tools/live_batches/release_qual/offline/final_lint.json
python tests/release_qual_test.py
python tests/batch_jobs_lint_test.py
```

The tests lint every source/final job against the frozen snapshot without reading live logs.
Only six required readiness queries and missing regexes were appended; existing frozen entries
and FPS/trace models were retained. Live-flow preflight also passed against 79 VALID logs. Final regeneration and CLI lint use
--frozen, retain no full-log cache, and create no file over 20 MB.

After coordinator commit, merge into the built test-main tree, explicit user steering and the
normal frozen tree/build/job authorization receipt, queue from the main checkout with exactly:

```powershell
Copy-Item -LiteralPath @('tools/live_batches/release_qual/final/jobs/final-s8-ultra-high.json', 'tools/live_batches/release_qual/final/jobs/final-stock-hair-dialogue1.json') -Destination captures/queue/pending/
```

That command is documentation only; it has not been run. The existing serial test-queue runner
uses test-main by default and resolves final plans directly from tools/live_batches/release_qual/final.
Do not start another runner. No entry is staged and no permission receipt has been created.
