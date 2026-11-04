# Stage 4 — generated rows and GI guard rejection screen

Prepared offline only. `python tools/live_batches/qual_stage4/make_batch.py` uses the
existing m4 jobs and shared live_flow retiming/readiness. No Git writes, game or queue.

01-camp and 02-inn are aliases of the repaired m4 generated-row qualification jobs.
Their deciding gates are retained intact. Run either these aliases or m4, never both.
Camp ~6.57 min and inn ~5.39 min including 120 s load and expected readiness.

03-guards-camp is the smallest fix-or-reject screen: warm both individual GI guard bits,
then OFF / mv-only / prev-only / OFF, with static pre-post float energy, held Q/E image
pairs, and diagnostic-free GI GPU cost windows. Reject a bit for release on energy loss
outside the OFF repeat spread or visible detail/temporal regression. If an artifact exposes
a concrete fix, implement and regate. A null or apparently improved PNG result does not
promote either guard. Missing registration, energy or cost evidence is INVALID.

The original transient gate measures 1-3-frame edge events: five calibrated K16 float
bursts (~11 GiB), analyzed by `python -m scripts.qa.pan_artifacts`, with FG evidence,
registered edge/interior rates and mean energy vs OFF repeats. A few PNG traces cannot
supply these samples. That gate is preserved in predicted.json and has not been weakened.
The lean screen can decide reject/fix only; original promotion remains unproven. No new
large capture protocol is prepared. The release owner must reject unproven bits if no
corrective hypothesis survives this screen, rather than claiming screening qualified them.

Run order: camp generated rows, then guard screen (separate launch to stay below 15 min);
inn generated rows last. No S8 launch is needed. Guard screen is ~6 min with load;
12 PNG traces, ~730 MB; all three jobs ~1.1 GB. Dryrun uses only fake runners:
`powershell -File tools/live_batches/qual_stage4/dryrun.ps1`.

Readiness waits reconcile cached consumer builds and require fresh pipeline quiet. New
transitions lack historical timing witnesses; timeout is INVALID. Expected minutes are
planning bounds, not live receipts. No live authorization or quality promotion implied.

Cold-wait limit: each readiness wait is at most 40 s and fails INVALID if not ready.
Full job timeouts include every wait plus 120 s loading: camp 704 s, inn 633 s,
guard screen 611 s (the latter applies only to qual_stage4). No launch can run 15 min.
