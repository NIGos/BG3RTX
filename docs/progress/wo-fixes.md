## Morning water and camera fixes (branch wo-fixes, OFFLINE_VERIFIED, 2026-11-03)
- No commit, Git metadata changes, game launch or real queue submission. ABI unchanged; shader request delta 0.
- camera_probe: removed centre_hold (one delta then stationary); legacy + hold_drag at 1/2 px remain.
- Separate 2 s static fg_snap=0 rearm before every capture; 8 phases / 80 s / 48 presents (was 7 / 110 / 80).
- Crash forbid now requires exception 0x; scanned all 84 tracked jobs and make_job.py files: no broad pattern remains.
- Water shots: 10 -> 16 s, trace_at [4,7,10]; plan 225 -> 249 s, conservative load budget 7 min.
- Every Python post uses py -3; m4/m5 gates put repo first before importing tools; foreign-package regression passes.
- Reflection ordinary/hit layouts and specialization pipelines survive 0/2 toggles; generation refresh and release retained.
- Compose permits F-1 only with shift into its camera space and matching lighting list/TLAS/material identity.
- Current-frame matching path stays valid; unshifted, mismatched, empty, future and older frames keep the probe.
- CPU frame-consistency matrix and both fake-queue dryruns pass; gates and thresholds unchanged.
- shader_identity --pp-diff main: 232 unchanged requests (209 inputs identical, 23 pp-identical), 0 compiles needed.
- waterhit --sample 16 --baseline main: 9/9 layouts, 96/96 comparisons, 9 key-off baseline matches.
- watershift --sample 16 --baseline main: 15/15 layouts, 60/60 comparisons, 15 key-off baseline matches.
- Final build + 128 CTests passed; git diff --check passed.
- Kept off in presets. GPU cost unmeasured; no quality pass. Existing budget remains +1.0 ms median/p90.
- After separate live authorization: S8 Water Queens House; inspect continuous turns, repeated captures,
  layout skips after repeat toggles, and hidden/offscreen compose shading > 0 in every ON window.
