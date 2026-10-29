# Session handoff - 2026-10-29
Read this first, then docs/STATUS.md and the top of docs/PROGRESS_CODEX.md.
STATUS describes stable rendering; PROGRESS_CODEX is the current work log.

## Goal and hard rules
Advance P1 through P6 until Saturday 2026-10-31 at 20:00 Europe/Rome.
Then write the final progress summary, push main/work branches and stop.
The user resumed on Oct29; the overall goal remains ACTIVE.
- Never modify user saves, force-push, rewrite history, or alter snapshot tags/backups.
- Never use Ponytail, its skills, hooks or instructions.
- Before EVERY launch check USER_PLAYING, actual BG3/runner processes and queue state.
- A marker or unowned game means offline work only. No concurrent games/runners.
- All games run through the serial hidden queue with explicit expect patterns.
- Source, binaries and HEAD of the active job tree stay FROZEN until terminal.
- Merge only through scripts/merge-green.ps1 after inspecting deciding numbers; push both branches.
- QUALITY R0: derived gates, quality before speed on Ultra; research defaults stay off.
- No FG/HDR-UI/swapchain work unless needed by the active phase.
One usage reset is authorized AT EXHAUSTION, not used yet. Latest read: weekly
usage 93%, one reset available. Do not consume merely because it is eligible.

## Stable code and checkouts
Main: C:/Users/quali/Documents/Code Projects/BG3RTX, origin NIGos/BG3RTX.
Rendering remains 6dba0a8; P6 design merged 441904a. No P1/P3/P4 rendering merged.
Use EXPLICIT workdirs: the app may still point to the unrelated P4 checkout.
- P1: C:/Users/quali/.codex/worktrees/en2-selfcheck/BG3RTX, codex/p1-pan 725955f, pushed, paired ABI14.
- P3: C:/Users/quali/.codex/worktrees/p2-pending/BG3RTX, codex/p3-box b97477b, pushed.
- P4: C:/Users/quali/.codex/worktrees/p4-omm/BG3RTX, codex/p4-omm 8d902d5, pushed.
- P6: p6-water/BG3RTX, codex/p6-water cd04c63, pushed.
Old research: pan-motion-vectors/BG3RTX 929be7a; motion-jitter/BG3RTX 53f0925.
Import only needed work, no unrelated FG. Old-SHA map is preserved under
D:/BG3_Tools/git_backup/commit-map-old-to-new.txt; never rewrite that history.
Backups D:/BG3_Tools/snapshot-20261028 and snapshot-20261027/* are untouched.

## Live job and immediate next action
287 LIVE frozen P3 b97477b: S7 cap1 integer unknown/cookie-count control.
Runner20748 started05:31:48. Verify actual process/session and terminal result;
do not restart from observation timeout. P3 source/binaries/HEAD stay FROZEN.
P1 286 recursion-off isolation still FAILS original PRE (+0.4292%, CI excludes0);
broad paired CI is not equivalence. P1 725955f clean/pushed, no candidate approved.
After287 terminal record actual counts, full capture finite checks and limits.
Next P1 source-epoch/conditional mismatch remains unresolved; no transport tweak
or lighting-model reduction is justified by these failed isolation controls.

## P1 - pan instability remains OPEN
User sees lighting/shadows detach while panning; Ctrl+Shift+F9 restores native.
Fixed-camera scores cannot close P1. Never compare images across independent
moving trajectories. Use exact-camera per-run residual or live toggles in ONE run.
Do not block on aggregate AO counters; actual saved PRE alpha and same-build
key-off controls are authoritative. QA uses SDL client coordinates/actual image
sizes and neutralizes events for StaticCamera; no desktop-resolution constants.

Fresh candidate reuses existing B2/B5 and immutable PRE pose through actual
submission. No new camera transport. On rejection of AVAILABLE legacy colour,
fresh source is native current lighting PLUS immutable PRE GI with the same
material/strength/skin/spec-share convention and ray weight/PDF/denominator.
Original missing/nonfinite-colour cohort stays fixed in BOTH gate modes; newly
visible colour outside that cohort is a known limitation, not solved shading.
POST changes RGB, preserves PRE alpha exactly, then composite publishes metadata.
Metadata availability token is GPU-owned; stride3 avoids source-toggle allocation;
128MiB range guard and typed actual-submit pins apply. All research keys default0.

Important evidence (specs and docs/evidence preserve exact values/provenance):
- 242/243 and249 old rejection loses71.8-99.9% and70.66/87.96% GI energy. No win.
- 252/253 static tests INVALID (~300px drift); SDL event fix254/255 proves static.
- Direct-only255 changes the integrand. Complete-source256 fails energy;258 low power.
- ONE-run259 residual+7.24%;260 residual-7.60% but cohort energy-90.19%: no gain.
- Exact-diffuse262 fails original PRE target;263 moving residual+2.60%: no gain.
- Geometry265/266 rendering INVALID: base passes lacked origin40; never reuse for quality.
- 7e91e6d shares base20/40/48 layouts AND writes.269 full layout/push/write audit passes.
- Projection268/269: current ray/B2 offset0 with nonzero jitter. Retain native positions.
- 274: previous projection/native MV agrees with canonical jitter on ground; tent animates.
- Corrected271/272 static12: POST/PRE+0.6630/+0.8620%; independent POST stream alone fails.
- 276 ONE moving geometric gate: residual+20.95%, Y+44.51%, motion+78.03%; unequal cohorts.
- Grid coverage276 is3.856/2.646% (millions of samples), not old0.08%; no quality gain.
- 280 gi_ao0 isolation still fails: gate+1.1697%, PRE+0.8004%, both CIs exclude0.
Removing native AO folding alone does not explain drift. No cap removal/AO workaround.
Existing271 PRE alpha below the give-back denominator0.001 covers3.2255%; exact0
only0.0211%. Receiver frequency is not source-energy attribution or causal proof.

Mode4 attribution (docs/GI_REJECTED_SOURCE_ENERGY.md) is diagnostic only:
old/fresh Y*w/rays on the SAME rejected ray, including actual failed-old energy.
Requires fresh RR full-res, original diffuse source/shared stream, fallback!=2.
Push32768, stats20, Q24 sums with uint64 carries;201 counters. Receiver-bin
population uses actual dimensions/ray count; invalid inputs are reported, not capped.
No source/output/visibility/weight/camera change. Modes1-3 keep their old probes.
Build12 CTests/24 ABI pass; seven ordinary GI/RF shaders are byte-identical.
282 initial run INVALID:9 failures/9.094M replacements, invalid0; reasons close.
Mixed weighted old/freshY0.004275577/0.004360368. Full layout/push/write categories
absent; other VVL categories UNRESOLVED, never claim globally clean validation.
Off capture has53 RGB differences (+0.00353% mean): no exact control parity claim.
On+1.908% versus PRE. c3b924f restores original selection condition and measures
failure energy before quantization: unlit zero-old samples differ from lost
positive bounces. No return/fallback is changed just to make an expect pass.
284 FAILS that distinction: unlit old energy is positive (up to6.73e-9), but
not the main positive drift1.40e-4. Off45/54 RGB differences remain; no parity
waiver. On+0.859/+0.688% PRE; full layout/push/write audit passes, other VVL
unresolved. Next gi_strength0 static12 isolates recursion, not a production fix.

P1 candidate is parked, not approved: PAUSED.md, GI_FRESH_FALLBACK.md,
GI_DIFFUSE_EXACT.md, GI_GEOMETRIC_NORMAL.md, GI_FRESH_INDEPENDENT.md,
GI_NATIVE_AO_ISOLATION.md. After an approved candidate, inspect static ground/
building edges while panning, toggle its key and Ctrl+Shift+F9. Local/sun/RF
pan gates also remain open. No prototype may be promoted from execution alone.

## P2 - pending merges
Local-light reference/gates merged6dba0a8 after two fresh hold90 repeats:
25 windows each,8.173/8.173B pixels, zero differences; build/3 CTests/32 shaders/QA pass.
EN2 hit materials (old worktree-agent-a94065fdd50bc8494) still fail blue/metalness/bias.
D-EM (worktree-agent-a2212c228ec492214) AF family304be1c7 still fails0.361/0.362.
Both stay off. EN2 root HANDOFF is stale; actual spec is TRACK_D_RT13 and
layer/hitmat.cpp / shaders/rt/rt_hitmat.hlsli. See docs/P2_PENDING_MERGES.md.

## P3 - box lights remain off
264 DEFAULT point/spot reference PASS:24 windows/7.845B pixels, zero differences.
250 FULL variant bit differences remain open, never waived by DEFAULT success.
267 activates native set3/two offsets; precision upgrade uses Q24/165 counters.
270 has no positive signal.273 GI0 still zero because debug4 prep forces AO0;
native direct flag1 suppresses direct terms too.275 debug6/AO1 confirms positive
inputs over3.236B samples, invalid0, but native ambient/in-scatter remains.
277 ONE AO1 box0/1: seven/nine pure windows, absolute difference falls67.3994%.
Ambient and nonzero overshoot1.258e-6 prevent a total native-accuracy claim.
278 S6 debug0 normal GI: ten windows,1.319 rays/px,0.738 occluded/px; final finite.
279 cap1 did NOT exercise unknown boxes (0.000/px): coverage INVALID, no fallback proof.
281 S7 exits2s before layer/scene; bounded retry285 succeeds, actual cookie0.004/px.
Sampling is exercised, not yet native-filter accuracy or unknown-pair proof.
283 S5 PASS: ten windows,0.079 rays/px,0.029 occluded/px; final finite.
S6/S5 cookie rates0; native accuracy, cookies, unknown coverage and moving/visual
quality remain open. D32F nonfinite dumps are retained, not a global finite pass.
See docs/P3_BOX_LIGHTS.md and evidence278-283. Do not modify saves or reinstall mods.

## P4-P6
P4 has a conservative CPU interval classifier and corrected stochastic-alpha
contract:197,621 independent checks/4 CTests pass. Sampling-domain provider,
GPU bake and OMM backend are NOT implemented. Mip0/1-only plan is unsafe;
unknown keeps native alpha. See docs/P4_OMM.md; no runtime/merge claim.
P5 SHaRC v2/light tree/EN2 consumers depend on EN2; sharc-v2 stays off.
P6 minimum water design DONE: docs/P6_WATER.md, merge441904a through merge-green
(build/3 CTests/32 shaders/QA). Runtime water and COVERAGE gap1 remain open.

## Queue workflow
Main captures/queue/results.jsonl and logs are authoritative; never leave results.md open.
Before EACH launch: USER_PLAYING + actual CIM bg3.exe/bg3_dx11.exe and test-queue/
auto-run processes + pending/running. One job only; fresh checks again after terminal.
Runner uses its own tree scripts, Main -Root, hidden window and explicit SDK/data env.
Preserve paired host/core ABI; do not reuse a process between different phase trees.
Copy validation.log to a named ignored build file before another validation run.
Inspect ALL layout/push/write categories (including07988), not sync errors alone.
Record numbers and scoped limits in Main PROGRESS_CODEX, then push work branches/main.
