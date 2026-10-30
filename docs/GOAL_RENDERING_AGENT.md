# Goal to give the execution agent

Paste the following into the intended BG3RTX agent. This file does not start a
different conversation, transfer an existing Goal or authorize a game launch.

```text
/goal Implement the BG3RTX improvements in docs/RENDERING_EXECUTION_PLAN.md through small evidence-backed increments: faithful native coverage; bounded scene reconstruction; supported offscreen materials/lights/state; improved reflections and traced GI; and a bounded opaque PT reference with a real-time go/no-go. Keep the working renderer and experimental GUI controls synchronized. Select water/environment work by impact. Whole-game PT, caustics and volumetric PT are not required. Work offline by default. Run/control the game ONLY after explicit user steering for a bounded batch or iterative window. Prepare the smallest decisive batch after its dependent offline checks pass, request permission then, and continue unrelated offline work while waiting. Old approvals, marker absence and queued jobs are not permission. READY_FOR_LIVE is not live qualification or feature completion.

Read docs/HANDOFF_SESSION.md, top of docs/PROGRESS_CODEX.md, AGENTS.md, docs/RENDERING_EXECUTION_PLAN.md and docs/PT_FEASIBILITY_OFFLINE.md at startup. Reconcile HEAD and existing owners. Thereafter refresh only relevant/changed references and maintain concise evidence links in the plan. Latest user steering overrides older instructions.

If a prerequisite has a demonstrated technical blocker, the accepted alternative is the plan's early no-go package: minimal reproduction, independent evidence review, alternative routes examined, supported hybrid gains, NOT_REACHED dependent milestones and an exact reopening condition. Do not invent room/PT artifacts to meet the goal. Missing live permission or invalid tests is waiting, not a technical no-go or feature completion.

The handoff files currently live in C:/Users/quali/Documents/Code Projects/BG3RTX and may still be uncommitted. Read that workspace copy first if your isolated worktree lacks them. Preserve unrelated changes; do not assume creating a worktree transfers uncommitted documents or grants permission to change an active test tree's HEAD.

Delegate bounded offline work and independent verification using isolated branches/worktrees, single-writer ownership and bounded resource load. One coordinator owns integration, ABI decisions and serial live release; workers can own settings/GUI/schema edits. Do not duplicate other agents' work, modify an active player/test tree or run competing games.

Milestones are a dependency graph, not a waterfall. Keep the current fix lane moving while independent asset/material/light work proceeds. Split M3 providers and scene state into small increments. A supported reflection/GI improvement may use existing trustworthy geometry and explicit native fallback without waiting for a complete world or every special material. Workers may author settings/GUI changes in their own branches; the coordinator resolves only conflicting edits and integration.

Start with the smallest existing coverage fix and a minimal fake-tested correction in the existing queue policy. M0 gates live release only; do not build a new authorization platform. A separate offline worker starts with one ordinary mesh and material/VT mapping; test HLOD, construction or terrain when the selected scene depends on them. Preserve the working renderer. Do not start a whole-world exporter or PT engine before the room/data gates pass.

Reuse the existing queue, live phases, QA, pan captures, shader compiler, asset readers, settings tests and merge gate. Extend tools only to supply a concrete missing observable. Require exact source/build/settings provenance and explicit invalid/unknown/refused results. Fix proved defects; do not guess at caster identity or hide errors using blur, bias, darker history or hair exclusion.

Automate same-run/in-frame A/B, camera plans, capture, analysis and setting restoration within an approved batch. Do not ask the user to edit INI or toggle flags. Keep unauthorized jobs outside watched pending inboxes. Keep user preferences unchanged. Update common/settings.h, generated defaults/schema and the launcher's Experimental controls for meaningful feature flags; leave them off in presets until qualified. Diagnostic switches need not become user controls.

Before requesting live permission, provide candidate revision/tree, hypothesis, named scenes/jobs, duration and data estimate, automatic comparisons, acceptance/stop criteria and rollback. Do not ask per job within the approved scope. If live permission is pending, continue useful independent offline work. When none remains, leave one concise pending request and exact restart instructions; do not poll, fabricate success or silently broaden scope. Manage Goal status only under the actual lifecycle tool rules.

Use an authorized iterative fix/test window without renewed approval for each permitted build, recording/freezing each individual job. Run focused checks during edits and mandatory full checks at integration. Build the smallest useful cache-free reference alongside M3/M4; M6 extends it. No report or review ceremony is required per small edit.

If live permission is pending or declined for now, continue every useful feasible offline increment across the plan, not just the current milestone. Reassign workers to independent implementation, asset/material decoding, existing-capture analysis, focused tests, GUI/schema and draft validation. Before stopping, check the remaining increments and state which exact missing input blocks each useful next step. Keep provisional fixture-based work distinct from real-provider qualification. Do not busy-poll, repeat unchanged tests, invent tooling to fill time or override an explicit user pause/cancellation.

Use scripts/merge-green.ps1 for integrations, inspect deciding numbers, preserve small default-off steps and push main/branch after a permitted merge. Preserve saves, backups, tags, shared junction targets and known-good builds. Never use Ponytail. Report what changed, evidence and costs, what remains unsupported, and the next concrete action.
```

If an unfinished Goal already exists in the receiving conversation, the agent
must reconcile this mandate with it and respect the Goal tool's lifecycle rather
than silently replacing unfinished work. This mandate is not permission to discard
the existing coverage fixes.

## Live steering example for a later prepared batch

The user can authorize the agent's concrete proposal with language such as:

```text
You may now run the prepared M1 batch <batch ID>, on <candidate revision/tree>,
using its stated scenes, presentation mode, duration/repeat limits and stop rules.
Do the toggles, captures and analysis automatically. This does not authorize
other live batches or changes to my saved settings.
```

This is an example, not actual authorization. The receiving agent records the
real user's instruction and applies its real scope; no exact magic phrase is needed.
When a batch is complete or revoked, its grant must not authorize unrelated jobs.
