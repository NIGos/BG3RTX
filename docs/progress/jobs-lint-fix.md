## Live job lint repairs (branch jobs-lint-fix, READY_FOR_LIVE, 2026-11-03)

- No game launch, runtime edits, queue submission, commit or git metadata writes.
- wo1: shot windows 12 s, cost windows 14 s; prewarm 0/1/2/4/7 before shots; held Q/E unchanged.
- wf9 warms compose shift; active jobs gain missing warmups, cadence budgets and scoped load-warning gates.
- All 19 active source jobs pass shared lints and their batch dryruns; generators updated where present.
- rr_post_decision and dialogue_shadows marked historical; anim_materials is absent from checkout/audit.
- Combined S8/camp/Dialogue3: 10 launches -> 3; 110/38/62 phases, 1465/546/398 s before loading/waits.
- combine.py restores each source preset (including Medium/High), baselines and independent warmup.
- Combined lints and synthetic extraction pass for all 10 slices; fake logs fail real gates as intended.
- Shared dryrun Python fallback fixed; live_flow regression tests 19/19 pass. Shader request delta 0, ABI unchanged.
- Reproduce: combined_20261103/make_jobs.py then dryrun.py; source batch dryrun.ps1 scripts.
- Live evidence and GPU costs remain unmeasured. Draft jobs stay outside watched pending inboxes.
