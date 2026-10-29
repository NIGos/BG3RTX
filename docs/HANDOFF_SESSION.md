# Session handoff — P1 static PASS, motion INVALID, 2026-10-29

Read AGENTS.md, then the top of PROGRESS_CODEX.md. No Ponytail or subagents.
Read D:/BG3_Tools/USER_STEERING.md (two 13:57 entries and 14:29 authorization).
FG 2 decides P1; FG 0 is reference only. The user sees the bug only with FG on.

## Permission and checkout

Latest user instruction authorizes visible/non-activating real-present FG jobs
without USER_AWAY. Never force focus; skip USER_PLAYING or an unowned game.
Never create USER_AWAY. Launch only scripts/test-queue.ps1; never hold results.md
open. No source/binary/HEAD edits while a game job runs. Restore display settings.
No saves, backups, snapshot tags, force-push or junction cleanup.

Branch codex/p1-pan-flashes in the main workspace. Main still 58b06b4; no merge.
Tested build c5a909c, ABI 16. Both fix defaults remain 0:
- rt_scene_rebase (314840d): consistent translation between lighting and TLAS space
  at all four ray-query entries. Native pool and dynamic XFB use camera-relative
  world axes. The frame epoch of each instance row is not established.
- fg_current_destroy_only (9bc8e70): historical-tag destruction no longer resets
  FG stability; current input destruction still does. Fence retirement unchanged.

Tool commits this continuation, pushed:
92cc6ad authorized no-Away FG mode; fe17233 observe coverage in authorized mode;
0a6a8cf native windowed extent; b5a2e87 cheap primary/epoch checks and recorded
RT dispatch counts; c5a909c restore 6 GB trace budget.

## Actual queue results — stopped after invalid motion verdict

QuickSave_3784, real presentation, window_mode=visible_unfocused_fg, FG 2 cold-load.
No observed activation, client fully covered throughout, native windowed 3840x1600.
Game closed, display restored, queue empty. Do not relaunch unchanged motion input.

Static p1-fg-static-effects.1.152122: PASS, 13 phases / 30 traces.
FG 2 normal primary 99.5195-99.5215%, 18.7-18.8 rendered fps, ratio 2.00;
FG 0 99.516%, 18.2 fps, ratio 1.00. No interior toggles. Sun/local/GI/reflection
recorded 120/120 and RR GPU timing; no persistent debug parity differences.
GI debug is black in both modes: parity only, not proof of visible GI energy.
Composite writes debug diffuse into previous radiance; app dumps do not certify
DLSS-G generated images. All five sheets under captures/p1-fg/static-effects-sheets.

Motion p1-fg-motion-matrix.1.153616: analyzer FAIL, qualification INVALID.
56-phase job deliberately stopped after 13 complete phases plus partial phase 13.
Posted 60 px/s rotation has stationary windows and jumps up to 98.58 deg/frame.
Baseline FG 2 median displayed/rendered 1.00, so sustained generation was absent.
All sampled phases have active RT 120/120, none 0; missing unrun phase evidence
is expected after stop, not an observed RT outage. No visible-flash certification.
First negative FG 2 rotation, baseline / rebase / destroy-only:
- match medians 21.299 / 85.984 / 20.5465%; static floor 99.501%
- fps 26.0 / 25.3 / 26.6; displayed/rendered 1.000 / 1.163 / 1.412
- options calls 5 / 3 / 1; interior mode changes 0 / 1 / 0
FG 0 reference matches 17.434 / 75.819 / 15.887%, ratio 1.00.
Different prior paths/views confound cross-variant median attribution.

6,106 retired-only destructions reset FG with guard off; 3,279 do not with guard on.
Three current-role destructions still reset with guard off. No SDK focus-disable
or crash exception. Guard policy does suppress real unnecessary resets, but neither
candidate passes P1 and interior-toggle reduction is unproved. Defaults stay off.
No merge and NOT ready for user check. Only merge-green if future valid gates pass.

Retained sources: captures/queue/logs/<job names above>.{log,runner.txt,status.json}.
Reports: captures/p1-fg/{static-effects-result,motion-gate-result}.json.
Curated: docs/evidence/p1-fg-live-20261029.json, with 14 measured phase summaries.
CloseMainWindow was deliberate; Invalid window handle followed during runner
teardown, not a game crash/timeout. Earlier origin-rebase 242 s exit still unknown.

## Next, bounded

First repair or qualify background relative rotation input, without moving the
physical cursor or forcing focus. PostMessage reaches the game, but controlled
speed/direction is not established. Check relative mouse/warp semantics before
changing renderer code. Preserve actual FG generation and >=18 fps requirements.
Then run controlled FG-on static/rotation A/B; FG 0 remains reference only.
The 56-phase fixtures/generator remain; jobs are not requeued after the verdict.

New hypothesis: translation rebasing alone is insufficient. Residual pool/soup
mismatch and scene/lighting pose separation are consistent with older native
instance/dynamic geometry data or camera-buffer epoch mismatch. Exact frame lag
and Reflex/FG causation remain unproved. Do not add another camera selector.
App QueuePresent increments g_frame once; SL generated presents call next layer
and update displayed counters, no app increment. New token/ReflexSleep follows
app present; initialized FG 0 retains that path. No speculative Reflex fix.

## Cost and checks

Same FG-2 static debug phase, full per-ray stats off at 15:02:53 until 15:03:18:
15.5 -> 19-20 fps, GI ~6.8 -> 0.379 ms, LS ~5.25 -> 0.530 ms. Full statistics
have major cost; historical 15.040 vs 4.269 ms are not matched settings/scene/
presentation. RR on in both. RT 2.872 ms is a mixed 300-frame interval.
Source p1-fg-static-effects.1.145628.log. No isolated renderer regression claim.
Primary/epoch checks now work with rt_stats/ls_stats/rf_stats=0, while ordinary
shaders remain byte-identical. Recorded counts are commands, not per-ray totals.
20 matrix/static/plan tests, 26 policy/geometry, 18 camera/permission checks pass.
Prior build 7 CTests, 21 ordinary shader identities, 20 variants passed.

Default visible_unfocused stays FG 0 only and requires USER_AWAY. Explicit
visible_unfocused_fg preserves FG 2/0 and follows latest no-Away authorization;
it uses SW_SHOWNOACTIVATE, WS_EX_NOACTIVATE and HWND_BOTTOM, no focus setter.
Native windowed extent avoids UI/hudless 3840x1600 vs borderless swapchain
3840x2160 mismatch. Coverage/foreground are observed, not aborted in that mode.
