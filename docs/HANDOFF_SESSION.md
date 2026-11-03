# Session handoff - 2026-11-03 evening

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md), then [GOAL_NEXT.md](GOAL_NEXT.md).
Full folded evidence: [evening archive](archive/PROGRESS_20261103_evening.md).
Rendering main eca2023b, ABI 26; bundles 6, 7 and 8 merged. This branch is docs-consolidate-1103.

## Resume conditions

- Lives are paused by the user until testing is fixed. No launch or queue submission is authorized here.
- Testing fixes are on main, offline verified. Their first live use must still confirm actual behaviour.
- Strand live enable can lose the device: stale soup address after elastic capture shrink.
- Current user steering places its fix on claude/hair-reload-fix; the local ref does not yet expose it.
- Do not enable strand hair until that fix lands. Earlier reload safeguards alone do not clear this blocker.
- Finish dependent offline checks, prepare outside watched inboxes, then obtain fresh steering for the concrete batch.
- Re-lint actual plans and dryrun manifests. Do not reuse oversized combined drafts or historical duration estimates.

## Next queue, in order, after authorization

1. Land and verify the pending hair crash fix. Then run Test Dialogue1 reload bisect:
   tools/live_batches/hair_validation/jobs/hv5-reload-bisect.json.
   Check survival/frames at native + scalp, physical origin, groom and primary stages after F8.
2. Real Ultra smoke: tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json.
   Keep qualified keys on; do not enable unqualified hair merely to enlarge the smoke.
   The earlier OMM/box smoke passed; the latest combined smoke was INVALID and needs replacement.
3. S8 Fresnel A/B: tools/live_batches/water_surface_fresnel/jobs/wsf1-s8-interface-column.json.
   Restart to provision rt_water_surface_fresnel, then same-run toggles; inspect pool rim and reference partition.
   Report compose cost. This changes the shading model, so an equal-mean gate is inappropriate.
4. Pak join camp: tools/live_batches/cov_paks_compact/jobs/cp4-compact-camp-join.json.
   Restart the layer; require discovery and UV participation plus full witness/resource/recipe gates.
   Discovery is fixed; candidate UV matches alone do not qualify material publication.
5. rr_post_temporal Dialogue3: tools/live_batches/post_temporal/jobs/pt-dialogue3.json.
   User sees much steadier DoF; S8 cost about 0.12 ms. Both ON windows need valid energy/quality evidence.
6. Hit-cost quality: tools/live_batches/water_offscreen/jobs/wo1-s8-hit-shade.json.
   Sparse reflection about 2.1 -> 0.53 ms is exploratory. Require post-water energy, coverage and zero truncation.
7. At the first use of each repaired path, verify duration and fail-fast behaviour from runner/log evidence.
   Check preflight, derived windows, loading/waits, 15-minute cap, silent producers and failed-window aborts.
   Confirm combined slice rebasing/session fault invalidation, settings restoration and repaired water captures.
   Do not trigger deliberate game faults; use offline failure fixtures for destructive failure cases.

## Open branches and dependencies

- claude/hair-reload-fix: pending stale-soup crash fix, offline regression/build checks, then hv5 survival evidence.
  Earlier module-map/load guards, resident keys, LOD ring, card ownership and sealed entries are already merged.
- hair-cards-retire: preserve work; retiring cards depends on safe, visibly complete strand coverage and valid hair gates.
  Native cards remain costly; no blanket strand promotion. Batch 519/1,217 passed; 698 remain under study.
- anim-materials: unmerged. Branch history records repaired key-off equivalence, 45/45; publication remains blocked.
  Lava/sprite work needs packs with current geometry, publication proof and dependent checks before a live request.

## Rules that matter now

- Follow [AGENTS.md](../AGENTS.md). Never touch saves, backups or snapshot tags; never hold queue results open.
- Launch only through scripts/test-queue.ps1 after explicit steering, never while the user plays.
- Same-run A/B, held Q/E turns, every-window gates, patterns and expect. Missing evidence means INVALID.
- Use float pre-post energy; reconstruction must preserve it. Lower energy is not better stability.
- Inspect GPU pass ms, not covered-window frame time/presented captures. Keep diagnostics outside cost windows.
- Target 2-5 minutes/scene; cap launches at 15 minutes. Mine invalid runs before rerunning.
- No builds/heavy compiles during cost windows; no lives during merge-green. Heavy DXC is machine-wide serial.
- Water energy/coverage fixes need a host restart. Failed shader requests are excluded from prefetch manifests.
- rr_post_temporal, Fresnel, hit-cost and SHaRC remain unqualified/off; Medium trace scaling still lacks quality gates.
- rr_hair_resolve and CPU perf keys were rejected. Do not requeue them. Dialogue AO is accepted.
- Remove worktrees only with scripts/remove-worktree.ps1; shared junctions forbid recursive cleanup.
- Branches write docs/progress/<branch>.md; keep this handoff <= 80 lines and progress about 150 lines.
- This consolidation changes documentation only. No game, code, shaders, commits or Git metadata writes.
