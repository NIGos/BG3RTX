## Live diagnosis fixes (branch live-gate-fixes, offline verified with caveat, 2026-11-03)
- Hair: scene-wait gate excludes diag-* only; regenerated 4 independent jobs, retaining all other diagnostic gates.
- Box availability warning waits until background builds finish; persistent unavailability still logs.
- Box energy: unresolved recovery is reported as inconclusive (exit 0); fixed below OFF minus noise fails.
- OMM diag-check: live rt_scene_wait_ms=100, 0-timeouts every-window gate; reset to 12 ms before costs.
- Audited 81 job/generator files; only om4 had the bad fault forbid. It now matches actual fault-info reports.
- OMM CPU readback retains each slot's scene freshness: stale queries become not compared, with no violations/candidates.
- No shader, ABI or preset changes; shaders/variants.txt request delta 0. No pp-diff required.
- Build passed (46 s); all 127 CTests ran: 126 passed, shader_identity_test failed on unchanged common/box_light.h CRLF/LF blob IDs.
- New energy-exit regression and OMM fold regression passed; QA detectors and git diff --check passed.
- Hair, box_lights_art and omm_qual dryrun.ps1 passed using a temporary py -3 shim for installed Python.
- No game launch or live measurements; GPU cost unchanged in scope, not measured. No commit or git metadata edits requested.

## Dialogue gate repairs (branch live-gate-fixes, offline verified with caveat, 2026-11-03)
- Excluded diag-view-* from judged scene/capture gates; static-only ON holds 12 s, trace at 8 s.
- Occ availability warning now waits for pending builds, matching the existing box guard; removed both job forbids.
- Motion triplets at 6/12/18 s, 22 s windows. Regenerated 4 jobs and 2 plans; baseline static-only is off.
- Tonight 04:14-04:49: moving shadow not seen by default with fix 2; port duplicates with differing mesh offsets 16-18 per frame.
- Static-only removes all cascade casters, equivalent to casters off: rejected as a default, retained only in explicit diagnostics.
- Dryrun passed with temporary py shim; 39.8 min, 2182 MB estimated, cap 2200 MB. Generated-job assertions and diff check passed.
- Core rebuilt in 4 s; CTests 126/127 passed (86 s); unchanged box_light.h CRLF/LF blob mismatch remains. No shader requests or ABI changes, no game launch, no git metadata edits.
