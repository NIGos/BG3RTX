# M1 coverage batch — BLOCKED DRAFT, never released

No receipt, pending job, game launch, GPU operation or live configuration change
is authorized by these files. The current user requested offline work until the
PC is free. The coordinator must resolve the automatic-observation gaps below,
receive actual user steering for the finite batch, select a frozen built candidate
and only then create its exact authorization receipt. Do not stage these drafts.

Renderer source baseline: `a9f8688` plus `a676bf5` (generation-matched OMM wiring),
`2e049e4` (wide-capture partition feedback) and `c26f831` (Sun fallback counter).
The experiment sets `rt_omm=0` to isolate native Hair, Scalp and current Sun.
`tree: "."` means that future frozen candidate's queue root, not an arbitrary
current main checkout. Freeze the eventual job, plan, tree, both ABI-19 DLLs and
source identity together. This draft does not provide a binary fingerprint.

## Two jobs and existing input support

1. `preflight.job.json`: load the named `Test1` reference, then execute the
   `preflight.plan.json` through existing `auto-run -Move -MovePlan` support.
   Static native coverage A, opaque control, native A repeat, Sun debug1,
   local visibility debug3, then a static production-cost window. Five timed F10
   requests use existing `trace_at`; no burst capture or raw-float dump is enabled.
2. `motion.job.json`: depends on successful preflight. Reload the same resolved
   Test1 reference, calibrate static native coverage, rotate at 60 and 180 client
   pixels/s in both directions, hold a final static window, then one slow rotation
   cost window. Rotation is existing owned-HWND middle-button PostMessage input.
   Requested movement must be confirmed by native GPU epoch/yaw measurements.

Both launches initialize `rt_native_hair=1`, `rt_native_scalp=1`,
`rt_sun_cascade_current=1`, `rt_cascade_casters=1`, `rt_bvh_split=1`, `rt_alpha=1`.
The profile explicitly selects a 1408 MiB RT capture budget with existing
60% adaptive alpha partitioning and the wide-stream feedback correction. This
restores the old total vertex capacity for the 88-byte stream; it does not
silently redefine a user's MB cap. Physical allocation failure or any deciding
capacity warning/alpha-made-opaque/opaque-dropped draw invalidates the test.
These startup controls never change during a run. The same-run coverage control
changes only `rt_alpha_test=0/1`; the opaque control does not disable RT or native
capture. FG remains requested at 2 throughout, with real foreground presentation.
It requires a user-authorized free-PC interval and the runner's usual owned-game,
USER_PLAYING and display-restore protections. Unfocused or headless execution is
not a substitute for actual FG2. There are no manual INI/dialogue steps.
The current foreground runner also stops when USER_AWAY is absent. The future
coordinator must satisfy that existing marker policy under actual user steering;
a marker is not permission. An absent prerequisite makes the batch INVALID.
No user action is requested by this draft.

## Preconditions and hard INVALID cases

Test1 must automatically restore the affected face pose and the intended distant
caster view. Its contents were not inspected. Loading a nearby exploration save
and expecting somebody to start the dialogue is not permitted. Required retained
target examples are camera Hair FS `5a03e5b678b2d040` and Scalp FS
`9e77575a9f377ec5`, observed in the user's 161807/162008 captures. Their new native
records and relevant traced pixels must be nonzero in the actual loaded view.

Queue expect/forbid checks run after a job, so static preflight must precede any
motion. Missing targets, missing pose proof, failed save load, no real generation,
incomplete readbacks, an unchanged camera despite input or an unavailable private
Sun tuple makes preflight/motion INVALID, never “no artifact” or a quality pass.

**Current release blockers:** existing logs report native record totals and only
the top 12 FS hashes, not a face/distant screen ROI or an assertion that Test1
restored its dialogue pose. A target omitted from that top list is unproven.
The existing `fg_motion_gate` now has a bounded `--coverage` mode that reuses
its phase/FG/primary parser and qualifies native Hair/Scalp/Sun, capacity,
current-TLAS and actual coverage-control evidence. It reports a separate
`log_gate`; the full `gate` remains INVALID without automatic pose/ROI proof.
Both drafts attach it with `post_gate=true`, so an unproved preflight cannot
release motion. Existing rebase/full FG-matrix analyzers are not substituted.
Missing pose/ROI is an explicit unavailable observable, with no manual override
or invented positive assertion. This is not a request for a new framework or
user work.

The companion source correction fixes Sun fallback counter 22: it now counts
lit-side `!current_covers`, including UNKNOWN, instead of the contradictory
`traced && !current_covers`. The eventual candidate must include that correction;
older zero fallback fields are not proof. Keep unknown-candidate/cone/packet
counts and require `certified - native_white_without_ray > 0` as well.

The dedicated current-Sun CAS material dispatcher does not yet honor the
baseline/procedural opaque-control bit. `rt_alpha_test=0` is observed for the
camera Hair/Scalp/control path, but must not be called a full opaque control of
current-Sun caster materials until that narrow control branch is corrected.
The analyzer does not invent proof of that effect from a requested toggle.

## Deciding evidence once the blockers are resolved

- Use complete phase-interior windows, excluding at least 15 seconds after each
  live setting change and the final 10 seconds. Statistics that overlap a boundary
  cannot be assigned to that mode. Exclude F10 collection/stall intervals from
  cadence and timing measurements as well. The 90-second quality windows accommodate the
  existing 120-frame primary and 600-frame Sun reports at the minimum 18 rendered
  fps. The two 60-second debug windows supply views, not independent scalar gates.
- Prove active RT in every deciding window: complete primary/epoch readbacks,
  `TLAS this frame` nonzero and `none=0`. For a strict same-frame Sun check,
  `previous frame` is not proof of a current tuple. Require actual same-submit Sun
  insertions, positive attempted selected/next ray samples and
  `certified - native_white_without_ray > 0`, with relevant certified support.
  Attempted samples alone include aborted queries. Native-white/no-ray pixels,
  all-UNKNOWN fallback or an unready scene
  never pass. Report fallback, unknown-candidate, cone-escape, auxiliary and
  frame/camera/ray rejection counts separately, including face/distant ROIs.
- Hair/Scalp records and target FS must remain present. Missing native inputs,
  unsupported required interfaces, invalid bounds or nonfinite/ray guards fail
  the target gate. Global positive counts alone do not prove a visible face fix.
- FG2 decides: SDK status/readiness, options calls, displayed/rendered ratio,
  rendered/displayed fps and focus messages must agree. Generated ratio must
  exceed the FG-off ratio of 1 in complete interior windows; a requested INI
  value or a single options-on call is insufficient. Any interior FG-off/focus
  transition invalidates that comparison; retain each toggle and its input reason.
- For alpha-test=1, moving primary match must stay near the scene's static
  approximately 99.5% baseline. Derive the lower bound from static repeat range
  plus one printed 0.001 percentage-point code, as the existing primary gate does;
  report the measured baseline and sample counts. Both measured yaw directions
  must exceed static precision/noise, and the faster requested input must produce
  faster measured rotation. Alpha-test=0 is an opaque coverage control; its
  cutout mismatch is expected and is not the moving primary-match quality gate.
- Opaque versus cutout geometry legitimately changes shadows and luminance.
  Do not require global brightness invariance between alpha-test=0 and 1. Use
  the native A repeats to bound encoding/jitter/noise, and check unaffected
  stable surfaces against that repeat bound. Face and distant masks must show
  the expected coverage change without stale/unknown substitution. There is no
  existing automatic ROI detector here, hence the blocker above.

## Duration, captures and cost scope

Preflight plan: 480 seconds. Motion plan: 630 seconds. Total scheduled scene time:
18.5 minutes, plus two loads/cleanup. Job deadlines are 660 and 840 seconds;
reserve a finite authorized interval of up to 25 minutes. A deadline, revocation,
USER_PLAYING or foreign-game event cancels owned work through the existing queue
protocol. Never extend the receipt or retry a failed configuration automatically.

Five PNG F10 traces are requested in preflight; motion requests none. `dump_raw=0`,
all burst serials are 0, and no raw-data analyzer/pruner is invoked. Existing
161807/161845/162008 PNG captures were approximately 40–51 MB each; five similar
captures suggest 200–260 MB plus metadata/logs. Reserve **1 GiB** as a planning
budget, not an enforced byte cap. Actual size depends on the restored scene.
The existing runner has no byte-budget stop; that limitation must remain explicit.

Cost windows turn off rt/ls/rf stats, primary checks and epoch diagnostics.
The existing `rt passes` timer still runs. Its scope is GI/AO/Sun consumer work;
local shadows, reflections and RR have separate existing GPU lines. Private Sun
BLAS/TLAS build commands precede that consumer interval and have no isolated
existing GPU timer. Production Sun-trust counters are also built into the path.
Report these limits: this batch cannot claim a complete isolated Sun build cost
or infer it from an earlier 4.269 ms versus 15 ms aggregate. No diagnostic and
production-cost windows may be mixed.

## Stop and rollback

Stop on the first INVALID precondition, missing/unknown RT, device/API fault,
premature game exit or failed motion proof. Keep logs, run status, phase markers,
captures and diagnostic reports; do not delete failure evidence. The queue closes
only its owned game and restores display settings. Per-run overrides are temporary;
all prototype defaults stay off. No merge, promotion or “ready for user check”
claim follows from an execution-only result. A later complete M1 quality gate
would permit only the final user confirmation, not manual setup.

Offline validation: `powershell -NoProfile -File docs/live-test-drafts/m1-coverage/check-draft.ps1`.
It reads JSON, checks known fields/controls/regexes and samples the existing pure
camera-plan function. It never imports a window controller or runs the queue.

Draft validation passed 632 checks on each of Windows PowerShell 5.1 and 7.6.5.
The coverage/legacy log analyzer regressions passed 27 tests, including its CLI
report and failure exit when automatic pose/ROI evidence is absent. No queue runner,
window API, save inspection, game or GPU was invoked. The unchanged actual
MovePlan loader separately passed multi/single/absent cases on PowerShell 5.1
and 7.6.5; no loader fix was needed.
