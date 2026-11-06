## DLSS 5 Release audit (branch dlss5-audit, offline fixes tested, 2026-11-06)

- Scope: NR provider, NGX frame contract/hooks, runtime staging and fixture review.
  No game, queue writes, staging, push, Streamline or common/fg_* edits.
- Fixed latent use of a feature whose GPU initialization was never submitted:
  wait without blocking for initial submit; recreate if that recording is discarded.
  Production retirement-function tests cover discard, pending submit and GPU lifetime.
- Fixed six timestamps continuing after statistics turn off with a retained pool;
  gated readbacks too. Quiet Release records/reads zero NR queries.
- Fixed watchdog reason ping-pong after 120 presents (extra flushed state logs).
  Empty submits/NR never recorded now avoid the NR mutex.
- Checklist: CPU waits OK/COST; release logging BUG fixed; policy OK; history OK;
  lifetime BUG fixed/COST; FG ordering OK. Details and source lines: TRACK_DLSS5 §9.
- User log: 18 unavailable-chain transitions, nearest rrpost reasons 14 unknown
  compute effect 4681b95ef9f2e54d, 3 bloom order, 1 chain start. Not proven menus/cuts.
  17 resume logs reset=1 are transition-only logs, not evidence of per-frame resets.
  A true one-frame skip requires reset: MV references the immediately prior frame.
- One feature create, zero recreates; three slot allocations only near startup.
  Private images 632.8 MiB observed, up to 1687.5 MiB for 8 slots, plus NGX/padding.
  Bypass/reset reuses resources; 27 descriptor writes and 400 uniform bytes/frame.
- Contract OK: output-sized 3840 x 1600 NR, guides from 2560 x 1067, MV -1/render,
  NGX MV scales output-sized, no MVJittered flag, inverted depth, exposure 1.
- Existing fx1/fx2 logs, all 49 windows/run: weighted total 6.438/6.524 ms,
  evaluate 6.241/6.325, encode+guides 0.123/0.126, decode 0.071/0.070 ms.
  No NR copy, extra submit, FG-specific wait or intentional FPS cap found.
  Eight of 16 image barriers precede NR timestamps; their boundary cost is unknown.
- Open: rrpost unknown-effect coverage (outside ownership), in-game vendor CPU
  latency/VRAM, cross-recording multiple real RR calls (no observed duplication),
  existing internal NGX validation VUID/Shutdown workaround. No live quality pass.
- Lean Release/DIAGNOSTICS=OFF build passes; shader request delta 0.
  Full build.ps1 -Test: 205/206 pass, CTest 141.86 s (Windows PSModulePath fixed).
  Only inherited batch_jobs_lint fails: snapshot lacks fx1/fx2 regexes.
  Its snapshot/jobs/test are unchanged from da6ffdf8; FG files left unchanged.
  Final incremental build + NR/logging tests 5/5 pass; lean Release rebuilt.
  Logs: .scratch/dlss5-audit-{final-test,final-regressions,release-final}.log.
- Future authorized check: S8 Ultra with NR on, move/toggle Ctrl+Shift+F8 and cross
  the transition that breaks rrpost; check detail/flicker and resume. No new job queued.
