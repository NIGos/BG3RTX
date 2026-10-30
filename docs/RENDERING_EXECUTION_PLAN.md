# Autonomous rendering development plan

User mandate: 2026-10-30. Execution specification for the receiving agent.
This file defines work and gates; it does not grant live permission.

## Outcome and scope

Improve BG3RTX through verified native coverage, reliable offscreen materials and
lighting, persistent scene coverage, traced GI/reflections and selected forward
effects. Establish a bounded opaque path-tracing reference and a defensible
go/no-go for real-time opaque PT with raster primary visibility. Whole-game PT,
general caustics and volumetric PT are outside the completion requirement.

The deliverable is working, reviewable rendering increments with evidence,
experimental GUI controls, automated validation and a reproducible PT decision.
Instrumentation alone is not success. A phase can end with an evidenced rejection
or scoped unsupported result, but cannot claim the corresponding feature works.

Read docs/HANDOFF_SESSION.md, the top of docs/PROGRESS_CODEX.md and AGENTS.md first.
Read docs/PT_FEASIBILITY_OFFLINE.md, docs/ARCHITECTURE.md, docs/QUALITY.md and
docs/QA.md once for orientation; refresh changed sections when needed. Before
each implementation read only the relevant TRACK/design and code, not all docs.
Handoff/progress describe current state; older roadmap tables are historical.
Reconcile actual HEAD and existing agents' work before making changes.

## Authority and the live boundary

1. Work autonomously on source, offline builds/tests, existing capture analysis,
   read-only package extraction, fixtures, GUI metadata and draft live batches.
   Use isolated branches/worktrees and output directories. Never deploy/rebuild
   into an active player's or queue job's tree, including live shader files.
2. Live means launching/controlling/instrumenting a game session, requesting new
   captures from it, toggling its settings or attaching a profiler. It requires
   explicit user steering for a named test batch/window. A closed game, an old
   approval, USER_AWAY, a queue field or a window mode is not such authorization.
3. This supersedes older user-led A/B instructions and historical launch exceptions
   for this development program. Do not ask the user to toggle keys, edit INI files,
   repeat camera pans or produce A/B screenshots. Automate the authorized tests.
4. Permission persists for the authorized batch within its stated scope across
   turns. Do not ask again per job. Do not expand its scenes, build scope, repeats,
   presentation mode, profilers or resource budget beyond that permission.
   Revocation or USER_PLAYING stops further live actions. Do not kill an unowned game.
5. Prepare drafts under docs/live-test-drafts/<milestone>/ or another explicitly
   non-watched directory. Do NOT stage unauthorized jobs in any captures/queue/pending
   inbox: an already-running queue can consume them. Never create USER_AWAY or
   erase USER_PLAYING to manufacture permission.
6. Only scripts/test-queue.ps1 launches approved tests. QA run is launch-capable;
   route it through an authorized queue command. Never call auto-run or QA run
   directly to bypass the queue. Live tests are serial; no parallel game instances.
7. Before requesting permission, finish the candidate's dependent offline work and supply one
   reviewable batch: hypothesis, candidate revision/tree, scenes, automatic phases,
   expected duration/data volume, checks, stop conditions and rollback. Explain
   precisely which remaining claim requires live evidence. Ask once per batch.
   Do not wait for unrelated offline lanes to finish: request as soon as a useful
   candidate is ready, then keep those lanes working while the user decides.
   The user may authorize a bounded fix/test window with iterative builds, not
   just one frozen revision. Honor that scope; record/freeze each job's actual
   candidate and checks without asking again for permitted iterations.
8. While permission is pending, continue independent offline work. Mark affected
   claims READY_FOR_LIVE, not qualified. Once useful offline work is exhausted,
   leave a concise checkpoint and pending request; do not poll or repeat the request.
   Follow the actual goal tool lifecycle; do not invent completion, pause without
   user instruction, or change blocked status before its tool conditions apply.

AGENTS.md prohibitions remain in force. Approved runner loading of an
existing named test save is allowed within the authorized batch; do not parse or
edit its files. Mod/profile changes need separate user authorization.

## Coordination and bounded parallel work

Use one coordinator and up to two independent offline workers initially, with
available capacity for review. Parallelism is a means, not a quota. Reuse existing
agents instead of duplicating owned tasks. Announce ownership and expected outputs.

Useful split: (A) current capture/coverage fix, (B) bounded asset reconstruction or
material CPU oracle, (C) independent tests/evidence review. Each worker has disjoint
files and its own checkout/build/output where mutations could collide. Shared SDK
and asset DB junction targets are read-only. Coordinate CPU/RAM/disk and GPU load;
GPU-only tools are not CPU-offline tools and must not interfere with player sessions.

The coordinator owns integration, host_api.h ABI decisions and live queue release.
Assign an owner for each shared settings/GUI/schema edit; workers may author those
changes in their isolated branches. Serialize conflicting edits/integration, not
every feature or all authoring behind the coordinator. Delegate
bounded tasks with inputs, acceptance checks, exclusions and a return artifact.
Review actual code and deciding evidence, not only worker summaries. Stop workers
whose output no longer helps the next gate. Do not launch speculative research lanes
while a concrete phase fix can be implemented.

Keep one main deliverable per worker. A stalled live gate releases that worker to
the next independent offline task. Use small agreed data contracts and fixtures
to let producers and consumers develop in parallel; mocks prove only the consumer
contract, and real-provider qualification still needs actual evidence. A review
can run alongside implementation or cover a completed small diff; a separate
reviewer is not a required ceremony for every edit. Do not add task servers,
mandatory daily reports or another ownership database: the short ledger suffices.

Live permission pending or declined for now blocks only live execution and claims
that need it, not the offline program. Before stopping, scan all remaining milestone
increments and reassign available workers to useful independent work: existing
capture analysis, native coverage/math tests, asset/material decoding, provider
contracts and implementations, shader compilation/parity, fake-runner checks,
GUI/schema updates and prepared validation batches. Do not wait for M1 live results
to start unrelated M2/M3/M5 work. Develop dependent consumers against explicit
fixtures where useful, but keep unverified assumptions and integration risks visible.
Use the existing ledger with next action and actual blocker; no new scheduler is
needed. Stop only when remaining useful work truly needs missing live data, another
external input, or unavailable resources. Explain those specific blockers once.
Do not fill the wait with repeated unchanged tests, speculative scope or invented
tooling. A refusal that explicitly cancels or pauses work takes precedence.

## Milestones and dependency order

States per independently testable increment: TODO -> IN_PROGRESS -> OFFLINE_VERIFIED
-> READY_FOR_LIVE -> LIVE_QUALIFIED. Offline-only increments stop at OFFLINE_VERIFIED.
Milestone IDs group work; they are not sequential stages that must finish in full.
Use REJECTED or UNSUPPORTED with evidence when warranted. Never equate a compiler,
CPU oracle, archive reference or fake-runner pass with game correctness.
The receiving agent updates this table with revisions, evidence paths and next work.
Use NOT_REACHED for a dependent milestone stopped by a demonstrated prerequisite
blocker; name that blocker. Missing live permission is waiting, not a technical no-go.

| ID | Initial state | Deliverable and acceptance |
|---|---|---|
| M0 | IN_PROGRESS: fake-tested scoped receipt and cancellation; coordinator review | Reconcile owners/baseline; make live release fail closed; prove authorization and draft isolation with fake tests. Preserve the existing runner. |
| M1 | OFFLINE_VERIFIED subsets; combined Sun/Hair build 35/35, live gates pending | Complete faithful Hair/Scalp/cascade cutout support and query consistency. Keep existing agents' ownership; reuse their work. Offline scalar/shader tests, missing-input rejection and default-off parity first; automated Test1 and distant-shadow qualification only when authorized. |
| M2 | OFFLINE_VERIFIED one mesh/VT (01db43b); affine room work in progress | First export one ordinary GR2 and decode its material/VT mapping. Build the smallest useful room subset with correct affine transforms, identities and bounds. Probe construction, terrain and HLOD separately; require each only where the selected scene/rays actually depend on it. Validate against native/captured evidence where available. No room-closure claim from reference counts alone. |
| M3 | IN_PROGRESS: native Base_AlphaTest_VT material decode; other providers TODO | M3a: supported hit-material provider; M3b: world-light provider; M3c: persistent static subset; M3d: runtime reconciliation. Start with declared subsets and independent contracts. Prove no duplicates/stale doors/removed objects and no camera-driven light/material loss where the consumer depends on those inputs. M3a/b do not wait for a complete world baker or every dynamic class. |
| M4 | TODO | Deliver offscreen shaded reflections and emissive/direct-hit GI, then traced SHaRC updates. Replace accounted native terms, avoid double counting, handle unknown traversal explicitly. Compare cache/reuse against a cache-free supported reference; validate motion, energy and cost. Small increments, not one cache-plus-lighting rewrite. |
| M5 | TODO, impact-dependent | Implement P6 water as a distinct interface/receiver; select cloud/environment and volume-shadow improvements by measured scene impact. Keep native unsupported components. M5 is not a prerequisite for an opaque-room PT reference. OMM is a separate measured optimization after alpha correctness and GPU proof, not a prerequisite to PT. |
| M6 | CONDITIONAL | Build a simple cache-free, unfiltered multi-bounce opaque PT reference over the verified room using raster primary hits; test BSDF sampling/evaluation/PDF, emission, MIS and termination. Then qualify an optional real-time candidate only if input, temporal and cost gates pass. Otherwise deliver the reference plus a precise go/no-go report and preserve the improved hybrid renderer. |

M0 protects autonomous execution, not a reason to delay reading/fixing known M1
code. M2 starts as a small decisive offline probe, not a whole-level world baker.
M3 is the shared investment; M4 delivers visible improvements. M6 may follow
M2/M3 without completing every M5 effect. Do not build a replacement engine first.

Use the smallest dependency graph that serves the next visible result:
- M0 and M1 proceed alongside the bounded M2 data probe; M0 gates live release only.
- M3a/b can start with existing captured geometry and documented test fixtures.
- A supported M4 reflection/GI increment can use M3a/b plus current trustworthy
  geometry and declared native fallback; it does not wait for all M2/M3 work.
- M3c/d expand the regions/rays where replacement is justified. M6 requires a
  verified room and the material/light/state subset its paths actually use.
- Develop the smallest cache-free reference alongside M3/M4 when it is needed
  to check their estimators. M6 extends that reference; do not wait for M6 to
  validate M4 or write two independent reference renderers by accident.
- M5 water/environment work can proceed independently where its inputs exist.
An unsupported class outside a consumer's tested domain does not block that
consumer; an unsupported input inside it must remain explicit and cannot pass.

Current isolated evidence: audited Hair/Scalp/Pubes coverage and current-cascade
Sun are default-off candidates. OMM GPU production/BLAS fixtures exist, but actual
renderer descriptor/Core selection is still unwired. SHaRC v1 is screen-fed; P6
water is design. One foundry GR2/VT is exported and validated (01db43b), with exact
native local bounds; this is not world/room closure or a material BSDF proof. Historical PT 5-8 ms
estimates are not acceptance targets. Derive tolerances and budgets from evidence.

## M0: close the actual authorization gap

scripts/queue-permission.ps1 currently contains an old visible_unfocused_fg /
-UnfocusedFG exemption. Background jobs also do not require fresh steering.
scripts/test-queue.ps1 scans root and .claude worktree pending inboxes. Those
mechanisms are NOT the new authorization boundary.

Implement the smallest extension of the existing queue policy that refuses live
execution without recorded user steering scoped to the approved batch. A receipt
records the user's actual instruction and scope; it cannot create authority by
itself. Bind released jobs to the intended tree/build, finite repetitions and
window/resource constraints. Consume/close the grant when the batch completes or
is revoked; never let it authorize an unrelated future batch. If the user grants
a broader window, preserve that actual scope rather than inventing per-job prompts.
Prefer a small batch allowlist/receipt and checks in the existing policy. No new
authorization service, cryptographic token system, scheduler or GUI is required.
Do not let M0 grow into general workflow infrastructure or block offline rendering
work. Until it is ready, keep drafts outside inboxes and do not release live jobs.

Test absent, stale/out-of-scope, revoked and valid grants, old window-mode bypass,
USER_PLAYING/unowned-process blocking, draft isolation and setting restoration.
Reject replay of consumed, expired or revoked grants after restart/session reuse.
A still-valid grant may continue only within its approved scope. Bind each job to
its actual frozen tree/build; permitted iterative builds need no renewed approval.
Reject out-of-scope candidates and differences from the job's recorded build,
not a new build explicitly permitted by the user's iterative window.
Exercise command, single-run and session entry paths;
authorization must protect every release/execution path, not only initial launch.
Use fake execution in a scratch root only. Do not modify/restart a runner while
an existing live batch uses it. Do not treat DryRun on the real captures tree as safe.
Keep policy and runner failures explicit; do not turn refusal into a silent pass.

## Reuse the existing tools

Verify each script's arguments in the current checkout before use. These are
entry points, not commands to execute indiscriminately.

| Purpose | Existing surface | Boundary |
|---|---|---|
| Build and CPU tests | scripts/build.ps1 -Test; CTest; relevant standalone tests | Isolated tree; non-trivial CPU logic needs tests |
| Shader compile / validation / parity | tools/compile_shaders.py; SDK spirv-val; existing native_scalp/rebase shader tests | Compile is not GPU execution; compare ordinary key-off SPIR-V against baseline using identical flags |
| Settings and GUI | common/settings.h; scripts/gen_settings_defaults.py; settings_test; tools/launcher | See settings contract below |
| Archive resources | python -m tools.assets; tools/assets readers; feasibility probe | Read install only; derived caches outside game and shared asset DB |
| Existing trace graph | scripts/analyze_trace.py <explicit capture> [--disasm] | Produces graph/report with heuristics; verify identities, not heuristic labels |
| Existing image QA | python -m scripts.qa analyze <explicit existing scene dirs> | Offline; preserve raw evidence needed by the question |
| Motion/pipeline isolation | python -m scripts.qa.pan_artifacts <bursts> --out <report>; existing pan fixtures | Real camera/scene headers; missing data is invalid, not zero |
| Coverage inventory | python -m scripts.qa.coverage with explicit inputs/output | Do not silently regenerate a shared document from stale/global captures |
| Queue simulation | scripts/test-queue-dryrun.ps1; test-queue-fake.ps1; tests/queue_window_mode_test.ps1 | Scratch root only; no actual pending inbox |
| Approved live execution | scripts/test-queue.ps1; live_phases.py; queue command for QA run | Needs user steering; obey mode compatibility and startup-only keys |
| Results | scripts/queue_results.py <batch-prefix> --lines; immutable kept log slices | Brief reads; do not hold results.md open |
| GPU-only offline investigation | tools/pipeline_stats.py; geo_blas_bench; actual OMM SDK probe | Check executable behavior and GPU availability; not game timing or full traversal proof |
| Integration | scripts/merge-green.ps1 | Coordinator only; inspect deciding numbers and obey no-game/USER_PLAYING guards |

visible_unfocused modes currently reject command/phases combinations; ordinary
visible_unfocused forces FG off. Do not assume a presentation mode can measure
the intended FG configuration. Use a supported mode or make a minimal fake-tested
extension. Shared/unfocused/background measurements do not establish focused FPS.

## Settings and GUI contract

No user is asked to edit INI or perform a test toggle. Job set/phases use isolated
captures/auto*.ini and verified restoration; never change bg3rtx_user.ini for a test.
Adding shipped key declarations/defaults to bg3rtx.ini as source is still required.

Every meaningful experimental feature gets a central settings knob with label,
description, supported range/choices, dependencies, hardware requirements and
live/hitch/restart semantics. Set experimental=true and keep it off in every preset
until qualified. Do not expose every diagnostic counter or one-shot switch.

The launcher already generates an Experimental section. A raw default key alone
does not create a control. schema_json currently omits experimental; add that field
when synchronizing status for tooling. Run gen_settings_defaults.py, regenerate
schema through settings_test --write-schema using the built executable's actual
path, and verify settings/schema/preset/read-write behavior. Build the launcher
in isolation and check layout, scrolling/height, DPI and dependencies as controls
grow. Launching the GUI for validation must not launch or reconfigure the game.
GUI visibility is not certification and must not silently enable a feature.
Ship the control/schema update with the meaningful feature increment before its
integration; workers need not rebuild/reinspect the GUI after every shader edit.
Reuse an existing suitable control rather than inventing a knob per implementation
variant. Recheck layout/DPI when controls/layout change, not for unrelated fixes.

## Accurate pipeline analysis without a new platform

Use a hypothesis -> required evidence -> smallest capture -> comparison -> fix
loop. Before writing a tool, document the missing observable and why existing
traces, counters, shaders and QA cannot answer it. Extend an existing capture or
analyzer where feasible. Do not build a generic debugger, telemetry service,
compiler framework or dashboard in advance of a blocked rendering question.
That justification can be one sentence in the existing task note or commit; it
needs no separate proposal, approval or document. Implement a proved small fix
immediately when existing evidence suffices. Capture only unresolved inputs needed
to decide the current hypothesis; a comprehensive replay system is not a prerequisite.

For a concrete bug, trace the relevant chain: pass/shader/material -> bound inputs
and transforms -> captured primitive/coverage -> BLAS/TLAS membership -> ray result
-> light contribution -> composition -> reconstruction. Stop at the first proved
defect and fix it; do not demand a complete pipeline census before every change.

Where needed for that chain, capture allocation/content generations, descriptor
bindings and dynamic offsets, draw/primitive/instance identity, camera/scene pose,
alpha formula/parameters, texture mip/residency, submission/completion epoch and
actual shader variant. Handles alone do not prove content or lifetime. Mark an
unavailable association UNKNOWN rather than guessing which tree/hair cast a pixel.

Capture selected frames/ROIs/streams with caps and explicit overflow/refusal
status. Record revisions, binary/shader hashes, effective settings, camera data,
formats/units/exposure and timing provenance necessary for the claim. Preserve
pre-post and final buffers when locating an artifact. Debug statistics default
off and are disabled in timing jobs. A full F10 without geometry identity cannot
prove exact per-ray attribution; add only that missing association if required.

Analyzers must reject truncation, missing phases, mismatched build/settings,
unsupported formats, stale epochs and absent expect lines. Test parser boundaries
and representative corrupt/negative cases; test physics/math against independent
or native references rather than only implementation round-trips. Distinguish
sampled agreement from continuous/filter/GPU proof, particularly for OMM and VT.

Use current shader source and native programs before proposing fallback recipes.
Screen derivatives, temporal dither and ray footprints need explicit contracts.
No hair skipping, enlarged sun, arbitrary bias/blur or darker history to conceal
known geometry/material defects. Unsupported providers keep a declared native
fallback where valid; they do not certify full path-traced coverage.

## Automated live batch and acceptance

Before release, draft jobs/phases that select explicit trees/scenes and contain
patterns and expect; use forbid, expect_runner, requires_success_of and post_gate
when applicable. Check phase semantics and startup-only settings. For statistics
requiring 120 frames use hold >= 90 seconds per repository rule. Bound repeats,
timeout, capture volume and expected result paths. Reuse existing movement plans.

Prefer in-frame reference comparisons for animated scenes. Otherwise compare
automatic same-run phases with verified pose and warm-up/history validity. Never
accept cross-run image differences as matched A/B. Automated A/B is still required
where it tests the claim; only the user's manual participation is removed.

Verify effective settings and successful capture/compute counters, not just requested
flags. No unsupported/refused/zero-record path may count as the feature passing.
Check finite values, API/resource errors, device loss, crashes, motion stability and
physical bounds. A completed runner with invalid post-analysis is not a passed test.

For unchanged transport, denoising/reuse/acceleration must preserve mean energy
within measured uncertainty; history rejection uses fresh valid samples. Genuine
lighting changes can change brightness versus native BG3, but their contribution
must be accounted for without probe/emitter/analytic-light double counting. Derive
thresholds from encoding, footprint and repeated baseline noise. Review visually
relevant crops as well as numbers; native pixels are not universal physical truth.

Use separate correctness and timing jobs when validation/counters change cost.
Report exclusive GPU pass ms, build/capture overhead, CPU cost, p95/p99/pacing and
memory as relevant. FG displayed FPS does not replace rendered-frame timing.

During iteration, run the narrow checks affected by the change. Run the required
build/CTest/shader/QA checks at integration through merge-green; do not duplicate
the full suite after each local edit. Reuse unchanged evidence only when its
revision/input dependencies still match, and rerun checks invalidated by a change.
Do not skip mandatory repository checks or invent tests for documentation-only edits.

## Integration, stopping and handoff

Keep short branches and small default-off increments. A CPU-only or default-off
increment may integrate through merge-green after required checks even while live
quality is pending, with that limitation explicit. Do not enable/promote a feature
or claim visual closure without its live gates. Push main and the branch after a
merge as required. Host ABI changes are centralized and minimized.

Do not integrate across an active user's game or bypass guards for convenience.
Freeze tested sources, shader files and binaries for a live job; settings change
only through that job's prescribed automatic phases. Source
development continues only in unrelated isolated checkouts. A changed build makes
the run invalid. Keep the user's known-good build available; deploy nothing merely
because an offline test passed.

After each milestone record: hypothesis/fix, revision, supported subset, key/GUI
status, commands and evidence, numbers/units, cost or unmeasured, pass/reject/pending,
rollback and next action. Keep handoff short and progress entries concise; link
detailed evidence rather than duplicating logs. The coordinator alone edits the
shared progress/decision ledger. Do not delete shared captures or caches as cleanup.
For a small increment this is one concise row/note plus links to existing output,
not a separate report in every document. Update TRACK when its design/results
change, progress at useful checkpoints, and handoff when the next agent's action
changes. Keep raw logs in artifacts, not duplicated narrative.

After an invalid result fix the test before changing theory. After a deterministic
failure, do not rerun it unchanged. Rerun larger suites only for changed inputs,
new failures or unresolved relevant concerns. Retire one-shot tools/flags when safe
without removing evidence; prefer reusing a small parser over a new abstraction.

M6 go/no-go requires explicit conclusions on world closure, live object state,
hit materials/VT, light policy, temporal reconstruction and measured cost. If those
do not pass, deliver the supported hybrid gains and the exact failing evidence.
Do not call whole-game PT impossible from a missing capture, or feasible from
archive counts. Completion of this program requires the milestone ledger and
supported outputs to match the actual evidence; deferred optional scope stays named.

An acceptable early no-go package contains a minimal reproducible blocker,
independent review of the evidence, the feasible alternative routes examined,
supported rendering increments retained, dependent milestones marked NOT_REACHED,
and the concrete new input/change that could reopen them. If room reconstruction
or its material/state prerequisites cannot be established, a room/PT artifact is
not required for that no-go outcome. This is completion of the bounded feasibility
decision, not successful PT implementation. Missing permission, an invalid run or
an unattempted available export route is not enough to declare that outcome.
Independent review is for the consequential final feasibility decision, not a
mandatory second agent for every locally unsupported case.

## Progress and decisions

- 2026-10-30: Plan authored from the user's autonomous-development request and
  read-only tooling audit. No implementation/live batch started by this plan.
- 2026-10-30 agility review: M0 gates live release only; M2 probes and M3 providers
  are independent increments. Request decisive live evidence without waiting for
  unrelated work; use focused iteration checks and delegate shared-file ownership.
- 2026-10-30 final review: independent read-only audit found no material blockers.
  Handoff consistency and 21 tool/source paths checked. Ready for offline execution;
  M0's runtime policy correction still needs implementation, not presumed complete.
- Live authorization: NONE for this plan. Historical exceptions are insufficient.
- Next receiving-agent action: reconcile existing owners; select the smallest M0
  and M1 offline increments, plus the bounded M2 export task if resources permit.
- Owner/revision/evidence ledger: receiving coordinator fills this section as work
  proceeds; an empty ledger is not evidence that implementation is complete.

Goal-writing rationale: use auditable milestones, durable evidence and explicit
completion boundaries rather than a vague improvement loop. [Official Codex Goals
guidance](https://developers.openai.com/cookbook/examples/codex/using_goals_in_codex).
