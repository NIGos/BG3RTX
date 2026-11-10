## hf1 job fix after the INVALID run 073932 (branch claude/hair-fg-job, staged, not queued, 2026-11-10)
- Cause of "window 'snap-fg3-a' has 0 event line(s)": a second periodic gate turned the snap windows into
  stats_reset frame windows, which start at motion-ready (07:41:30.365); `present capture done` was logged at
  07:41:29.932 (fg_snap is applied at the settings read, before motion-ready).
- Fix (tools/live_batches_staged/hair_fg/make_job.py): snap windows are plain 4 s timed windows gated on the last PNG
  line (`[sl] presented frame 11 -> ... (ok)`); each fg=3 watch window waits for a fresh `[sl] DLSS-G frame` line
  with displayed/rendered >= 2.5 (30 s, then `pipeline readiness timed out: survive-fg3-...`); zoom 20 wheel notches
  (was 10; head size not verified offline).
- FG after load: SDK cadence 3.00 two seconds after FG on (07:40:37); the layer's present counter 1.00 until
  07:40:50, 3.00 at 07:40:53.9; the log gives no cause. Live re-enable: 3.00 after 2.7 s, so window A had generated
  frames throughout.
- 97 s of phases plus waits (about 2 to 3 s per fg=3 window); lint 0 failing (4 duration notes: snap windows kept at
  4 s for the PNG writer). No launch. Details: docs/RTX_HAIR_PRIMARY.md 5s.
