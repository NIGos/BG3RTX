# Session handoff - 2026-10-29

Read this first, then the top of docs/PROGRESS_CODEX.md and AGENTS.md.
## Current state: USER-REQUESTED PAUSE

The user paused game activities for a live test, then requested this handoff to
change model in a new conversation. The thread goal is PAUSED, not complete.
Do not resume development/tests merely because this handoff was read.
Wait for explicit user permission to resume. Keep captures/queue/USER_PLAYING.
The latest read found no BG3/runner or pending/running job, but absence of a
process does NOT cancel the pause or authorize clearing the marker.
Preparing this handoff did not modify game code, binaries, settings or saves.

The original horizon is Saturday 2026-10-31 at 20:00 Europe/Rome. On explicit
resume, retain the full objective below; do not silently extend the deadline.

## Hard rules

- Never modify user saves, force-push, rewrite history or alter snapshot tags.
- Never use Ponytail, its skills, hooks or instructions.
- Before every launch check USER_PLAYING, actual BG3/runner processes and pending/running jobs.
- Games run only through the serial hidden queue, with patterns and expect checks.
- Freeze an active job's source, binaries and HEAD until actual terminal state.
- Merge features into main only through scripts/merge-green.ps1 after inspecting numbers.
- Push main and phase branches after integrations. Research defaults stay off.
- Never archive or clean a checkout with junctions to shared SDK/data folders.
- Backups in D:/BG3_Tools are read-only. Never touch backup files or snapshot tags.
- Background 120-frame statistics require hold of at least 90 seconds.
- Write short English sentences with normal spacing between numbers and words.

The one authorized usage reset was consumed at weekly 100% on October 29.
A fresh reading confirmed success. Reset credits: 0. No second reset without
fresh user authorization. The overall goal is currently paused by the user.

## Next priority after explicit resume: P2

The user's explicit P1 resume was carried out: complete fresh fallback, missing-
history correction, spatial and exact FP16 normal bounds, static energy and
same-run moving measurements. No attribution/exposure research displaced them.
There is no demonstrated quality gain. Under the goal's stalled-phase rule,
the priority slot advances to pending integrations. P1 remains incomplete and
its candidate stays off. That phase decision preceded the later user pause;
it is not a claim of success.

P1 checkpoint: codex/p1-pan dbd320c. PAUSED.md records evidence and a concrete
return point. Static 301 on/off +0.646% is within noise, but original-PRE
+0.460% excludes zero. All 36 captures pass finite GI, exact alpha and 23 GPU
predicates. Normal rejection is 13.24%. Full VVL is preserved; other errors remain.
Pan 303 verifies 8 fps (19 complete windows of 40 frames per 5 seconds).
Within ONE run, motion differs only +1.162%, but raw residual rises 21.951%
and cohort GI Y rises 11.193%. Samples 20.96 / 14.45 million; grid coverage
31.49% / 22.16%. No quality promotion. Trace median 0.548 ms; whole experimental
GI 64.288 ms. Raw fresh noise is not resolved-RR image quality. Resume with
same-run resolved-image evidence and a valid fresh source for unsupported inputs.
Never compare independent pan images or repeat unchanged tests until one passes.

## P2 next action: emissive facing hypothesis

No queued test remains active. After explicit resume, first check actual game/
runner processes and pending/running jobs; clear USER_PLAYING only when authorized
and idle. All launches still go through the queue. P2 branch codex/p2-emissive is at 7a87ba7, updated from current main. The merge
changed only AGENTS/docs; source and binaries are unchanged. It has owned
SDK/data copies, no shared junctions.
Paired D-EM ABI 9 differs from P3 ABI 9: never reuse their game processes.

Preserved diagnostic 295 was fully audited after the required P1 measurements.
Ten scene windows: 67,031 target receivers, 71,754 triangle candidates, zero
multi-draw receivers; distinct-draw sum equals receiver count. Invalid IDs,
overflow and missing candidates are zero. Nine actual native draw records show
blend disabled, back-face culling, EQUAL depth test, no depth write/dynamic state.
The coincident additive-draw hypothesis is rejected for that observed cohort.

Exact AF family 304be1c7 still FAILS: ratio 0.362 against 0.842-1.188, N 237.
Other five evaluated families pass this short diagnostic. Final captures report
zero nonfinite values; native D32F reports 2,764 / 2,587 and is preserved.
No scoped layout/push/write VVL tags, but other errors remain. Build median
0.496 ms; effects passes 0.118 ms with capture. No without-capture baseline.
295 used hold 45 before the new rule: not a complete promotion gate.
Full evidence: P2 docs/evidence/p2-emissive-overlap-295.json; full VVL archived.

Native SPIR-V confirms raw UV, colour/mask/intensity and pulse; prior UV/pulse
checks remain. A concrete mismatch to test: native back-face culling versus
two-sided emissive lookup with facing culling disabled. Multiple triangles from
one draw can still differ in facing. Reuse existing debug stats to count nearest
hit facing in AF and passing GF. Log native front-face state; respect captured
winding. No guessed sign, brightness scale, new resource or ABI. Sampler address
state is unrecorded; filtering tests do not prove repeat/clamp equivalence.
Read docs/P2_EMISSIVE_OVERLAP.md and TRACK_D_RT section 13 before implementing.
The existing rt_em_overlap_check push currently collapses nonzero to 1; mode 2
can report facing using debug words 8-14. Unused global word 15 can mark the
probe mode, so mixed in-flight windows cannot be mislabelled. Native state logs
need frontFace as well as cullMode. This probe is a PLAN, not implemented: the
current key supports the overlap probe only. Keep emission/ordinary shaders unchanged.

## Workspaces and stable state

Use explicit paths; the app may select another checkout.
- Main: C:/Users/quali/Documents/Code Projects/BG3RTX, main, pushed.
  Before handoff documentation: eb70825. All inspected trees were clean.
- P1: C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX, dbd320c, ABI 14.
- P2: C:/Users/quali/.codex/worktrees/p2-emissive/BG3RTX, 7a87ba7, D-EM ABI 9.
- P3: C:/Users/quali/.codex/worktrees/p2-pending/BG3RTX, b913e8e, box ABI 9.
- P4: C:/Users/quali/.codex/worktrees/p4-omm/BG3RTX, 8d902d5.
- P6: p6-water/BG3RTX, cd04c63.

Main rendering remains 6dba0a8. Water design 441904a and legal QA exposure-hold
fix 7a98949 are merged. Local-light reference was accepted after two hold-90
repeats with zero differences. No GI, EN2, D-EM, box-light or OMM candidate is promoted.
EN2 fails blue/metalness/luminance gates; SHaRC v2 depends on it. Keep off.
P3 run 291 has 0.09991% format-incompatible receivers among 2.950 billion;
native accuracy, cookie/unknown and moving gates remain open. No promotion.
P4 CPU classifier passes 197,621 oracle checks and four CTests; provider/GPU
bake/backend remain unimplemented. P6 minimum design is done, runtime water open.

## Remaining objective and working tools

Keep the original scope after resume:
1. P1: GI, local-light, sun and reflection pan instability. Each must meet its
   derived temporal gate or clearly improve with an explained cause. Write the
   in-game key/save/check instructions. P1 is NOT done.
2. P2: merge only passing EN2, D-EM and local-light reference work. The reference
   is merged; EN2/D-EM are not. Preserve failed gates instead of tuning scales.
3. P3: finish box-light native accuracy, shadows/cookies/unknown and motion gates.
4. P4: finish OMM beyond the CPU classifier: provider, GPU bake and backend.
5. P5: SHaRC v2, light tree and EN2 consumers, after EN2 is validated.
6. P6: water design minimum is done; runtime remains open.
Use stalled-phase checkpoints rather than calling unfinished phases complete.
After explicit resume, work autonomously; record meaningful results and evening summaries.
At the Saturday stop time, write the final progress summary, push all work and stop.

Use explicit workdirs. Runtime tests: scripts/test-queue.ps1 with Root set to main.
Main captures/queue/results.jsonl and its logs are authoritative; never hold
results.md open. Build/checks: scripts/build.ps1 -Test, tools/compile_shaders.py,
tests/qa_detectors_test.py. P1 has scripts/qa/gi_energy.py and
tools/validate_owned_motion_shaders.py; P2 has scripts/qa/em_gate.py.
DXC/spirv-cross/spirv-val: C:/VulkanSDK/1.4.357.0/Bin. Python: py -3.
Read each branch's design and preserved evidence before changing code. Do not
automatically resume old listed agents or background tasks.
Do not spawn sub-agents unless newly authorized by the user or applicable rules.

## Validation and cleanup

Preserve full validation.log before each new validation run. Inspect all layout,
push and descriptor-write categories including VUID 07988. Scoped absence is
not globally clean validation. Never keep queue/results.md open.
The archive tool once followed junctions and emptied shared SDK/data targets.
They were restored from read-only backups; all 992 SDK hashes and DB/manifest
hashes match. Recovery evidence is committed. Never repeat shared-junction cleanup.
At the Saturday stop time, write the final summary, push all work and stop.
