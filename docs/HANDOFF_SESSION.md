# Session handoff — 2026-10-29
Read this first, then the top of docs/STATUS.md and docs/PROGRESS_CODEX.md.
STATUS describes the stable code; PROGRESS_CODEX is the current work log.
## Goal and hard rules
Advance through P1 to P6, in order, until Saturday 2026-10-31 evening.
The stop target is 20:00 Europe/Rome: update the final progress summary,
push main and the work branches, then stop. Work autonomously in the meantime.
- Never modify user saves, force-push, rewrite history, or move/delete snapshot-20261027/* tags.
- Before any runner/game launch, check captures/queue/USER_PLAYING and live BG3 processes.
- The marker or an unowned BG3 process means the user is playing: do offline work only.
- In-game tests run only through the serial queue, hidden/click-through, with expect patterns.
- Merge features only through scripts/merge-green.ps1 after reading the deciding log numbers; push main and branches after each merge.
- One codex/<phase> branch from current main; cherry-pick only useful old work.
- QUALITY R0, derived gates, quality before frame rate on Ultra. New features off by default until their gates pass.
- Prefer fixes and short A/B loops. No FG/HDR-UI/swapchain work unless a phase needs it.
- Do not use Ponytail, its skills, hooks or instructions.
User explicitly resumed on Oct29. P1 energy preservation has first priority.
One usage reset is authorized if needed at the limit; not used yet.
## Main and research state
Main includes P6 water design; rendering remains 6dba0a8. No P1 feature merged.
Remote: origin, private NIGos/BG3RTX. Preserve all snapshot tags and backups.
Old pre-rewrite SHAs can be translated with
D:/BG3_Tools/git_backup/commit-map-old-to-new.txt; never rewrite that history again.
Old research is preserved at pan-motion-vectors/BG3RTX 929be7a; camera-native
53f0925 is in motion-jitter/BG3RTX. Import only needed work, no unrelated FG.

## P1 — Pan instability

User: RT lighting/shadows detach during pans; Ctrl+Shift+F9 restores normal.
Fixed-camera scores and cross-run moving images cannot close P1. Use exact
reprojection residuals or live off/on in ONE moving session. Do not block on
AO counters: saved PRE alpha and same-build key-off controls are authoritative.

codex/p1-pan: en2-selfcheck/BG3RTX 7433185, pushed, paired ABI14. All research
keys0. Main has no P1 feature. Existing B2/B5 and PRE pose pins through actual
submission are reused; no new camera transport. Metadata GPU token, depth AND
world-normal checks guard actual integer colour texel, not unused neighbours.
QA uses SDL client units/event cursor initialization; real-present movement stays hidden.

Old replay stays off: CURRENT_RADIANCE.md. Surface history: GI_HISTORY_SURFACE_GATE.md. Old242/243
loses71.8–99.9% bounce energy;249 point-load still-70.66%/-87.96%, alpha identical.
248 moving residual improves but cohortY-97.29%: no win. Do not reuse these as
quality passes. Cohort grew from0.08% to13.4%/7.15%; no arbitrary threshold added.

gi_history_fresh=0 is POST frozen-pose complete-source replay: accepted/control
uses original complete radiance; rejected available samples use fresh native
lighting PLUS immutable PRE GI at the source, material/strength/skin convention.
Original ray PDF/weight/count and PRE visibility remain. RR full-res/diffuse-only.
caae9b0 fixes BOTH-mode original missing-colour cohort;
no fresh samples added where original had no finite source. Gate0 control is
bit-identical in all six262 captures. Existing missing-lighting epoch limits remain.
Metadata stride3 in fresh mode holds surface/optional pan GI/exact diffuse RGB;
128MiB range guard and typed pins apply. Both source modes publish exact diffuse
so a source toggle never reallocates. Other surface modes use stride1/2.

GI_FRESH_FALLBACK.md preserves251–260. Highlights:252/253 static INVALID (~300px
camera motion); SDL event fix254/255 proves static. Direct-only255 internal parity
is PARTIAL (changed integrand). Complete-source256 original/noise energy FAILS;
258 both CIs include0 but a light pulse makes power LOW; bias not proved solved.
259 ONE moving run: residual+7.24%, Y+0.80%, coverage4.87%/6.82%, no pan gain.
260 four-ray: residual-7.60% but Y-90.19% with off spikes; no quality/variance claim.
Raw normalized residual includes stochastic noise and both-zero=0; not final RR.

Independent gi_history_diffuse_exact=0 removes legacy reflected-colour mixture
from GI, retaining complete RF radiance. Requires fresh1; surface gate0 isolates.
Metric2 buckets this source key; metric1 buckets surface gate. Both report Y/RGB
residual on same camera/MV/depth/normal-compatible moving grid4 cohort; GPU mode
excludes toggle frames. Source labels identify actual captured key state.
261 smoke valid117s, finite/pose/AO/static0.12px. VVL existing2647 hazards,
zero dispatch/private-handle/new-metadata messages: scoped, NOT clean VVL.
262 static12: off/on-0.20675% within noise; stricter PRE target FAILS pooled
+0.015598% (on+0.0311..0.0314%); control RGB exactly matches PRE. No scale/waiver.
263 exploratory ONE-run source pan: Y residual+2.596%, RGB+2.539%, cohortY+3.942%,
motion76.57/79.55px, coverage5.033%/6.675%. No gain/default/main promotion.
Spec docs/GI_DIFFUSE_EXACT.md; keep source exact0 for the next surface hypothesis.

New gi_history_geometric_normal=0 tests geometric identity instead of shading
normal detail at different raster sample centres. Requires fresh RT GI/rt_origin1;
ONLY matched non-aborted TLAS normals from existing owned image40 are accepted.
Missing/degenerate/nonfinite stays unavailable, no neighbour/shading rescue.
Depth AND world geometric normal must match with fp16/normalization/oct16/fp32
precision bounds, no angular fudge or new camera data. GPU metadata records mode.
Geometry runs265/266 are INVALID quality evidence: full265 VVL has07988 because
base temporal/composite layout and writes lacked origin40. Separate15-case math
kernel remains valid; do not interpret energy/rejection from those render runs.
7e91e6d shares base extras20/40/48 between layout and actual push writes; compiled
pointer/type contract test and header-derived reflection pass (12 CTests/24 ABI).
269 valid112s: ZERO full layout/push/write errors,15 cases/probes pass. Other
VVL categories unresolved; not globally clean. Spec docs/GI_GEOMETRIC_NORMAL.md.
268 isolated four-binding probe valid97s:15 cases and finite inputs pass; two
ray/projection offsets zero PRE/POST for3 captures despite nonzero current jitter.
Its surrounding render path was invalid, so this is CAMERA-ONLY evidence. Retain
native ray_at positions. Do not infer old-matrix/MV convention from that alone.
Self-test2 uses reserved row12 with exact24-bit packed fields; no new camera data.
7433185 default-off gi_history_fresh_independent separates POST stream3 from
PRE stream0, addressing possible nested-sample correlation without extra rays,
images/weights/AO/camera change.271 valid200s/alpha/static0.08px, off RGB exact. PRE target FAILS +0.6630%
(CI excludes0); on +1.03..1.77%.272 LIVE SAME frozen7433185 build with key1.
271 evidence is untracked to keep HEAD identical; commit both AFTER272 terminal.
Report CIs/power and saved alpha; control RGB differs intentionally by RNG.
Only then ONE moving gate toggle as evidence permits. GI_FRESH_INDEPENDENT.md.
Keep active checkout source/binaries frozen. Recheck user-playing/processes/queue
before every launch. P1 active offline while P3 ran; next runnable P1 gets priority.

For a future approved candidate build: inspect static building/ground edges while
panning; toggle candidate key, plus Ctrl+Shift+F9 as all-RT/native reference.
Do not close P1 until each effect meets its derived temporal gate or is clearly
reduced with cause explained. If stalled, PAUSED.md preserves evidence/next idea.

## P2 — Pending merges

Read each branch's HANDOFF.md and inspect its latest deciding logs/jobs.

| Branch | Feature | Deciding evidence |
|---|---|---|
| worktree-agent-a94065fdd50bc8494 | EN2 hit materials | 170-en2fix-S8-cov: joined > 0, coverage self-check, CPU wait |
| worktree-agent-a2212c228ec492214 | D-EM emissive | em12-af-check and af-view19/20: resolve AF 304be1c7 ratio around 0.36 |
| codex/p2-pending | Frozen local-light reference | Merged at 6dba0a8; fresh cap0/cap1 hold-90 repeats valid, zero differences |

P2 reference and gate changes are merged and pushed, with the phase branch.
Fresh deciding logs end 184221 / 184404: 25 windows each, 8,172,707,324 /
8,172,707,294 pixels and zero differences. Full build, all 3 CTests, 32 shader
files and QA pass. EN2 still fails blue/metalness/bias; D-EM AF family 304be1c7
still fails at 0.361 / 0.362. Both remain off. See docs/P2_PENDING_MERGES.md.
merge-green now runs all CTests and shader entries, checks main/playing/merge
state and aborts its own failed merge. Its failure/success fixtures pass.

## P3 to P6

- P3 codex/p3-box 3379d60 (p2-pending/BG3RTX), off.264 DEFAULT reference PASS24 windows/7.845B pixels/zero differences.250 FULL bit differences open.267 box activation valid291s, native set3/2 offsets, nonzero evaluations; accuracy INCONCLUSIVE from zero quantized means (fx1/256, scale0.00136865), not proof of black rendering. VVL unresolved categories retained. Q24 repeat270 INVALID:52 windows/16.919B samples, native/recompute nonzero0, invalid0. Native-AO probe prepared (GI0, box1/debug4,35s); no accuracy claim. Shadow rays/quality untested; no merge.
- P4 codex/p4-omm 8d902d5, managed p4-omm/BG3RTX: conservative interval classifier and corrected stochastic-alpha contract, 197,621 checks/4 CTests pass. No runtime integration or merge. Old mip0/1-only plan is unsafe; unknown retains native alpha.
- P5 SHaRC v2 after EN2, light tree, EN2 consumers in GI/SHaRC. sharc-v2 remains off.
- P6 minimum design DONE: docs/P6_WATER.md, merged 441904a through merge-green (build, 3 CTests, 32 shaders, QA pass), main/branch pushed. Water runtime and gap 1 remain open; phase branch codex/p6-water cd04c63.

## Test and reporting workflow

Queue: scripts/test-queue.ps1. Jobs: captures/queue/pending/*.json. Give jobs
both patterns and expect; check the complete kept log, not just summary lines.
Results: scripts/queue_results.py and captures/queue/logs. Do not keep results.md
open. Check actual processes, not PID files, for liveness. Verify PID start time
before treating any game as owned. No concurrent games or test runners.

While tests run or the user plays, do offline work, including the next phase.
Keep PROGRESS_CODEX in plain English with normal spacing: a few lines per result
and a ten-line summary at the top each evening. Keep this handoff near 150 lines.
Replies to the user remain concise Italian.

References: QUALITY.md, QA.md, TEST_SCENES.md, COVERAGE.md, DESIGN_SOTA_NEXT.md,
ROADMAP.md, USER_SETTINGS.md and the relevant branch HANDOFF.md files.
Modded Act 2/Rivington saves stay out of scope; do not reinstall their mods.
Backups: D:/BG3_Tools/snapshot-20261028 and snapshot-20261027/* tags, untouched.
