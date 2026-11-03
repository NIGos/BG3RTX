# Stage 4 lean draft: screening, not a qualification pass

No game launch, permission, queue submission, merge or commit. Predictions are written
first by `python tools/live_batches/qual_stage4/make_batch.py`. This does not refresh Git metadata.
The immutable main hash, branch tips and `main..<branch>` results are in merge_audit.json.
Main can advance independently; HEAD is recorded separately. Variants delta: 0.

One job/launch per save: camp 275 s, inn 174 s, S8 265 s, plus loading.
All jobs use visible_unfocused_fg. Q/E are held for equal four-second outward/return
intervals; no mouse turns. Check camera return registration before interpreting pairs.
Cost windows have no traces or expensive diagnostics. Keep the PC free of compiles/builds
during costs. All qualified Ultra settings, including OMM and box shadows, are retained.
Generated rows are enabled at startup to latch capture; live comparisons toggle rows.

The requirements conflict. Four submit CPU windows of at least 90 s already require
360 s before warmup. The existing GI transient gate requires five full K16 float bursts,
about 11 GiB, beyond the 2 GB batch budget. This draft therefore preserves the original
predictions/gates in predicted.json but is explicitly SCREENING ONLY for CPU/GI.
It cannot promote either candidate. CPU camp repetition and dedicated coverage/reference
and static energy comparisons also remain outstanding. M4 retains its earlier every-window
gates and has same-run float scene captures, but numerical energy/share/cost decisions
must still be read and recorded; matching regexes alone never qualifies it.

CPU: discard straddling intervals and first timing line after toggles. Require >=600
steady frames (hitmat/submit), >=900 (hooks), two complete hook samples and >=300 matrix
submits per state. Submit additionally requires >=90 s/state. Normalize watch totals by
presents, hooks by captured draws, matrices by builds. Derive uncertainty from OFF repeats.
Judge CPU ms, not frame time. Qualifying OMM coalescing also needs eligible tickets >0
and extension/Submit2 fallback checks; zero is not coverage. The lean windows cannot meet
the full qualification contract. Missing samples invalidate even screening observations.

GI: guard-shot-off-a/mv/prev/off-b provide pre-post scene means from dumps.json with
exposure held; never use PNG means. Q/E PNG pairs show visible motion behavior but do
not supply gi_luma transient rates. Original gate: prev edge rate below both OFF repeats
by more than OFF spread; MV edge/inner at most OFF max; static energy and inner rates
within OFF spread; GPU cost within OFF noise. Do not claim this gate from PNGs.

Dryrun: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/qual_stage4/dryrun.ps1`.
It uses the existing synthetic runner only, in TEMP, with no authority to run live.
The estimate includes 120 s loading/save (about 17.9 min total), 10 traces, 630 MB.
Loading is separate from the 2â€“5 minute phase target; no hard upper bound on load time is claimed.
Before any later authorized run: refresh/build from the recorded intended main, stage plans
only then, record explicit permission using existing queue tools, and keep one launch/save.
Queue runner restores the original INI on exit/failure; final phases also reset experimental
keys, exposure and CPU diagnostics. Do not modify saves or copy this draft into pending now.
