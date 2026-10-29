# Box lights — integration candidate

Branch codex/p3-box starts at main ee7dc56. Checkout currently lives at the
managed p2-pending/BG3RTX path; the completed P2 branch and evidence are retained.
This is offline preparation while P1 remains the active rendering priority.

## Implemented, not yet approved

Selectively ported the shader foundation from a28a4c7 as d088cdd, preserving
main's handoff and the corrected point/spot reference. Host wiring is 385d1fe.
The ls_box key defaults to 0 in development, generated defaults and release.
It selects separate default/full pipelines, binding the exact native lighting
descriptor set as set 3 with its recorded dynamic offsets. Missing box buffer,
set, layout or pipeline retains the existing point/spot path. ls_check and
ls_impl=0 also retain their existing reference paths.

Layout changes rebuild the box pipelines; release defers their destruction.
The stats allocation is 152 counters, covering shader indices 136–150, including
projector-only validation counters that also run in the ls_box=0 baseline.
The inherited 136-counter allocation was too small for those extra writes.
Projector validation selects every pixel whose froxel lists a projector in both
off/on modes; requiring an evaluated light would exclude box-only pixels from
the off baseline and compare different populations. Ordinary default and check
shader binaries are byte-identical to main with boxes disabled (DXC SHA-256).
Logs report actual box activation, evaluations, shadows, unknown queries,
cookies and projector-pixel recompute errors. GPU timing identifies box mode.

Build, all 3 CTests, generated-default agreement and 34 shader files pass.
These checks do not prove native descriptor compatibility, physical agreement
or visual quality in game. Baseline250 has run (details below); no box-on runtime or main merge yet.

## Deciding queue work

After P1's runnable candidate has priority, use serial S6, S7 and S5 sessions:

1. ls_box=0, ls_check=1, hold 90: every reference window must have zero any-bit,
   relative and one-writer differences. Keep current point/spot behavior.
2. ls_box=0 and 1 with ls_debug=4, ls_stats=1, hold 90 each: inspect complete
   pure windows, projector coverage, game/recomputed mean and errors. Derive
   accuracy gates from the same input/recompute contract, not an arbitrary
   brightness allowance. A missing projector population is inconclusive.
3. ls_debug=0 off/on: inspect geometry, shadow edges, leaks, finite output and
   temporal behavior. Compare the selected native shadow policy; unknown rays
   retain the game's atlas term. Quality has priority over frame rate.

Require activation and complete log windows, not only a requested key. Do not
waive failures because the rest of the frame agrees or because cost is low.
Before every runner/game start, check USER_PLAYING and actual BG3 processes.
Use the shared queue with expect patterns. Source and paired binaries stay
frozen while a job runs on this checkout. Merge only through merge-green after
reading the deciding numbers, then push main and this branch.

The cookie gradients approximate neighboring quad samples; verify their
agreement. Box lights have no fixture/owner join. No source-angle parameter is
invented for these directional emitters. The water and OMM phases are separate.

Queue specifications are saved in tests/fixtures/p3_box_s6_reference.json and
p3_box_s6_validation.json. Run the reference first and inspect every hold-90
window. The second job uses p3_box_validation_phases.json: one session with
100 s key-off then 100 s key-on, ls_debug=4 throughout. Inspect only pure
120-frame windows, identified by the mode switch and configuration markers;
discard a window spanning the change. Nonzero projector coverage and actual
activation are required. The expect patterns prove execution only: they do not
certify recomputation accuracy. Do not enqueue these behind an active P1 game;
recheck user-playing, actual processes and the queue before each launch.

## Reference fixture correction after250

250 on8d40fdc is execution/expect INVALID:21 windows,6,861,577,296 reference
pixels,12,304 any-bit differences, zero >1e-5-relative/one-writer differences,
zero driver faults/dumps. The fixture accidentally set ls_stats=1, selecting
the FULL variant. Earlier accepted P2 controls used the DEFAULT variant.
The reference fixture now explicitly pins ls_stats=0,ls_samples=0,ls_debug=0,
with ls_check=1 still allocating/reporting all reference-check counters.
No threshold is relaxed and every zero-difference window remains required.
This compares the actual default point/spot path to its accepted reference.
It does NOT resolve or waive FULL-variant bit differences; that separate
limitation remains open, including any mode that requires exact FULL agreement.
Projector native-light validation still requires actual coverage/accuracy in
its own full/debug path. Re-run baseline before activating any boxes.
P1 retains queue priority; fixture change alone is not runtime evidence.

264 /ec68966 DEFAULT-path reference: valid165s total/104s session, zero driver
faults/dumps/build changes. All24 complete120-frame windows have ZERO any-bit,
relative, one-writer and joined-result differences over7,844,924,963 reference
pixels. Default point/spot compatibility passes. This does not waive250's
FULL-variant bit differences or prove boxes; first box-on native projector
validation remains next once P1's runnable test has had queue priority.
Evidence docs/evidence/p3-default-reference-264.json.
