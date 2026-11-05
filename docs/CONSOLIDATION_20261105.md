# Consolidation 2026-11-05

Offline consolidation of the whole project before one final qualification live (user goal, 2026-11-04 evening).
Sources: three independent read-only reviews of main `2114f053` (layer; core, shaders and common; settings,
launcher, scripts, tools and docs). Every row is confirmed in the code by the coordinator before its fix lands.
"Suspected" rows stay suspected until a test shows them.

Status values: open, fixing (branch), fixed (commit), removed, deferred (reason), not a bug (reason).

## 1. Findings on the release path

| ID | Where | Sev | What | Confidence | Stream | Status |
|---|---|---|---|---|---|---|
| L1 | layer/tracker.cpp:258,679,702,959 | P0 | Resource destroyed with no covering fence ("not waited") | confirmed | cons-layer | fixed (bundle35) |
| L2 | layer/tracker.cpp:274,294 | P0 | Lifetime wait timeout, then "destroyed anyway" | confirmed | cons-layer | fixed (bundle35) |
| L3 | layer/streamline.cpp:2304 | P0 | FG consumption wait timeout only logged, then destroy | confirmed | cons-layer | fixed (bundle35) |
| L4 | layer/hotreload.cpp:459; rtscene.cpp:195 | P0 | 240-frame age used as GPU completion for deferred destroy and scene slots | suspected | cons-layer | fixed (bundle35) |
| L5 | layer/tracker.cpp:192 | P0 | Pruning uses another queue's completion | suspected | cons-layer | fixed (bundle35) |
| L6 | layer/streamline.cpp:2504,2513 | P0 | Failed FG disable still commits "not running", skipping waits | suspected | cons-layer | fixed (bundle35) |
| L7 | layer/ngx.cpp:566; rrpost.cpp:259 | P0 | Partial init reported complete on retry (`if (g_dsl) return true`) | confirmed | cons-layer | fixed (bundle35) |
| L8 | layer/hotreload.cpp:421; rrpost.cpp:244 | P0 | CreateImageView failure ignored, allocation reported OK | confirmed | cons-layer | fixed (bundle35) |
| L9 | layer/ngx.cpp:516, bvhsplit.cpp:141, rtscene.cpp:284, rrpost.cpp:238, water.cpp:268, emissive.cpp:298 | P0 | Bind*Memory results unchecked | confirmed | cons-layer | fixed (bundle35) |
| L10 | layer/water.cpp:1017,1063; emissive.cpp:1419,1455 | P0 | Begin/End command buffer failures ignored before QueueSubmit | confirmed | cons-layer | fixed (bundle35) |
| L11 | layer/rrpost.cpp:428,490 | P0 | RR descriptor ring (256) has no completion ownership | confirmed | cons-layer | READY_FOR_LIVE (layer-submit2; build -Test 196/196, shader identity 218/218): RR descriptors stay immutable until recording discard plus GPU completion; grow pages beyond 256 instead of dropping post passes; rel1-smoke-ultra-s8 temporal lines and grade > 0 pending |
| L12 | layer/ngx.cpp:825,936 | P0 | NGX guides marked initialized at recording, not submission | confirmed | cons-layer | READY_FOR_LIVE (layer-submit2; build -Test 196/196, shader identity 218/218): Each full-overwrite guide dispatch carries its own layout transition; initialization published only by exact submitted recording; rel1-smoke-ultra-s8 temporal lines and grade > 0 pending |
| L13 | layer/ngx.cpp:542,550; rrpost.cpp:901 | P0 | Guide view read without the mutex that guards resize | suspected | cons-layer | fixed (bundle35) |
| L14 | layer/assetdb.cpp:247,281,286,301 | P0 | Asset DB bounds checks can overflow (corrupt file) | confirmed | cons-layer | fixed (bundle35) |
| L15 | layer/alphatest.cpp:1211,1214,452 | P1 | Alpha mask init committed at recording time | confirmed | cons-layer | fixed (bundle35) |
| L16 | layer/alphatest.cpp:682,1182 | P1 | Teardown leaves dummy init latched for the next device | confirmed | cons-layer | fixed (bundle35) |
| L17 | layer/rrpost.cpp:1147,1117 | P1 | Temporal history accepts an unsubmitted producer | confirmed | cons-layer | READY_FOR_LIVE (layer-submit2; build -Test 196/196, shader identity 218/218): History provenance per ping-pong slot; delayed or discarded producers use fresh temporal samples without skipping temporal/final; rel1-smoke-ultra-s8 temporal lines and grade > 0 pending |
| L18 | layer/streamline.cpp:833; cmd.cpp:497 | P1 | Discarded FG recordings exhaust the 8 copy slots | confirmed | cons-layer | fixed (bundle35) |
| L19 | layer/shaders.cpp:53,115,515 | P1 | thread_local DXC COM pointers leak | confirmed | cons-layer | fixed (bundle35) |
| L20 | layer/util.cpp:85; shaders.cpp:400 | P2 | write_file ignores fwrite/fclose errors (shader manifest) | confirmed | cons-layer | fixed (bundle35) |
| L21 | layer/hotreload.cpp:21,262 | P2 | Core fault flag is a plain bool under shared locks | suspected | cons-layer | fixed (bundle35) |
| C1 | core/gi.cpp:851,869,876,2300 | P0 | Sun DSL created, pool/set failed: retry updates an unallocated set | confirmed | cons-core | fixed (bundle35) |
| C2 | core/emissive.cpp:199,201,204 | P0 | Partial layout init passes null layouts on retry | confirmed | cons-core | fixed (bundle35) |
| C3 | core/core.cpp:77,84,95,114 | P0 | Unused probe pipeline built every frame, results unchecked | confirmed | cons-core | fixed (bundle35) |
| C4 | core/gi.cpp:2261,2300 | P0 | Sun descriptor set reuse by frame age | suspected | cons-core | not confirmed: ring larger than frames in flight, no wrap with a pending consumer shown; core ABI has no completion token (post-release contract) |
| C5 | core/gi.cpp:739,747,750 | P1 | Layout leak on failed pipeline-layout creation | confirmed | cons-core | fixed (bundle35) |
| C6 | core/gi.cpp:787,790,1216 | P1 | Reflection layout change drops cached pipeline variants (leak) | confirmed | cons-core | fixed (bundle35) |
| C7 | core/ls_lights.cpp:192; sun_trust.cpp:138; water.cpp:479,1551 | P1 | Mapped rings read/overwritten without completion | suspected | cons-core | not confirmed: ring larger than frames in flight, no wrap with a pending consumer shown; core ABI has no completion token (post-release contract) |
| C8 | core/gi.cpp:2923,3606 | P1 | GI init/parity committed at recording | suspected | cons-core | not confirmed: ring larger than frames in flight, no wrap with a pending consumer shown; core ABI has no completion token (post-release contract) |
| C9 | core/water.cpp:1673,1683 | P1 | Water state read before the mutex | suspected | cons-core | fixed (bundle35) |
| C10 | shaders/gi/gi_common.hlsli:166 | P1 | Reprojection converts before the finite check; off-screen falls back to current pixel | suspected | cons-core | fixed (bundle37, cons-shaders) |
| C11 | core/water.cpp:1530,392,1072 | P2 | Rejected water hit layouts and pipelines prepared with the key off | confirmed | cons-core | fixed (bundle35) |
| C12 | core/gi.cpp:1641,3020 | P2 | Mapped counters consumed by frame age | suspected | cons-core | not confirmed: ring larger than frames in flight, no wrap with a pending consumer shown; core ABI has no completion token (post-release contract) |
| C13 | common/experimental_ab.h:13 | P2 | F10 cannot target Fresnel, the only release Experimental option | confirmed | cons-product | not a bug: F10 targets live options only; Fresnel needs a restart; the UI now says so (bundle37, cons-ui) |
| P1 | common/settings.h:1038,1250 | P1 | Rejected water shading/cost, alpha spill and pak publication stay selectable via "Show experimental options" | confirmed | cons-product | fixed (bundle35) |
| P2 | tools/launcher/launch.cpp:244 | P0 | GetEnvironmentStringsW null dereferenced | confirmed | cons-product | fixed (bundle35) |
| P3 | tools/launcher/web_host.cpp:265,331,379 | P0 | WebView init callbacks capture a stack Host by reference | suspected | cons-product | fixed (bundle35) |
| P4 | scripts/release/install.ps1:85,90 | P1 | ZIP install/update replaces files with no rollback | confirmed | cons-product | fixed (bundle35) |
| P5 | scripts/release/install.ps1:79,81 | P1 | Interrupted collision backup adopted on retry | suspected | cons-product | fixed (bundle35) |
| P6 | tools/launcher/launch.cpp:38,297 | P2 | Read failure treated as EOF; Save can overwrite settings | confirmed | cons-product | fixed (bundle35) |
| P7 | tools/launcher/launch.cpp:52 | P2 | Flush failure reported as saved | confirmed | cons-product | fixed (bundle35) |
| P8 | tools/launcher/launch.cpp:156 | P2 | Game-folder save failure hidden | confirmed | cons-product | fixed (bundle35) |
| P9 | tools/launcher/web_host.cpp:321 | P2 | CreateWindowW unchecked (possible hang) | suspected | cons-product | fixed (bundle35) |
| P10 | tools/launcher/ui_product.h:107,395 | P2 | Native fallback UI ignores the Experimental opt-in | confirmed | cons-product | fixed (bundle35) |
| P11 | common/settings.h:1269,1272,1452 | P2 | Release policy turns OMM on after the hardware clamp turned it off | confirmed | cons-product | fixed (bundle35) |
| P12 | tools/launcher/launch.cpp:201 | P2 | Developer env vars (BG3RTX_ASSETDB, _SE_DIR, _SAVE_LOG) leak into player launches | confirmed | cons-product | fixed (bundle35) |
| T1 | scripts/merge_scope.py:88; merge-green.ps1:140 | P2 | Shader+tooling bundle skips tooling gates | confirmed | cons-product | fixed (bundle35) |
| T2 | scripts/test-queue.ps1:498,507 | P2 | Cancelled job descendants survive | confirmed | later (lives paused) | deferred: developer queue only, lives paused; next queue tooling change |
| R1 | layer/geocapture.cpp:1099,1110 | P2 | Diagnostic pipeline retries leak (not release path) | confirmed | cons-layer | fixed (bundle35) |

## 2. Debt

| ID | What | Action | Stream | Status |
|---|---|---|---|---|
| D1 | Rejected code still compiled: pak publication, generated rows, IP grass, alpha spill, water hit shading and bits, current sun, half resolution, GI guards, 3 GPU candidates, SHaRC (40 of 169 shader requests) | Remove after the fix streams, one candidate per commit, default-path shader identity each time | cons-cleanup (after layer/core) | removed (bundle37): 8 of 9 candidates; SHaRC kept (its device capability serves water and hair; separate first). Shader requests 167 -> 122; release path proven equivalent |
| D2 | Missing unit tests: asset DB malformed input, resource retirement, NGX trampoline decoder, FG lease, experimental A/B, light join, launcher launch plan, ZIP install failure paths | Added with the fixes that touch them | per stream | fixed: tests added with each fix (asset DB, retirement, FG lease, init failures, launch plan, ZIP install, A/B, shader math); 172+ CTests |
| D3 | Obsolete comments (alphatest default, water headers, bvhsplit pool alpha, wf9 blocker, RR one ray, settings comments) | Fix with each stream's files | per stream | fixed (bundle35/37) |
| D4 | Obsolete docs: GOAL_NEXT, KEY_AUDIT_20261103, SETTINGS_DESIGN, GUI_AUDIT_20261101, RELEASE_CHECK_20261101, BACKLOG packaging section, developer README, USER_SETTINGS labels (8 rows) | Superseded notices; USER_SETTINGS and README match the launcher | cons-docs | fixed (bundle35) |
| D5 | Settings texts: Fresnel "awaiting qualification", rejected GPU candidates "awaits qualification", half-resolution "unqualified", OMM/masks guide says Ultra only | UI/UX review (step 5) | ui-review | fixed (bundle37, cons-ui) |
| D6 | Live batches still scheduling rejected experiments (regenerate_active, combined_20261103); batch READMEs without outcomes | Retire from scheduling, keep fixtures the tests read | cons-product | fixed (bundle35): active jobs 32 -> 17 |
| D7 | Native launcher CLEAN control remnants; launcher main.cpp comment | Remove remnants, fix comment | cons-product | fixed (bundle35) |
| D8 | Compiler warnings: the merge-green log has no core/common compile lines | Full rebuild with warnings captured, then fix | cons-core | fixed for core/common (0 warnings); layer/tools warnings listed in docs/progress/cons-core.md |

## 3. Later review streams (shaders, tools)

| ID | Where | Sev | What | Status |
|---|---|---|---|---|
| S-F03 | gi_composite.hlsl:51; water_compose.hlsl:144,363 | P2 | half-float radiance stores overflow | fixed (bundle37): clamped to 65504 |
| S-F04 | rr_post.hlsl:159-171 | P2 | geometric mean overflow turns bright pixels black | fixed (bundle37): log fallback |
| S-F05 | rr_post.hlsl:67 | P2 | R11G11B10 quantisation emulation ignores exponent limits | deferred: needs a verified GPU boundary fixture |
| S-F06 | rt_common.hlsli:390 | P2 | alpha page coordinates computed before the mip clamp | fixed (bundle37) |
| S-F07 | gi_trace.hlsl:59; gi_contact.hlsl:31 | P2 | UV == 1 loads one texel out of bounds | fixed (bundle37) |
| S-F02 | gi_composite.hlsl:48 | P2 | one scalar share cannot recover diffuse RGB | deferred: design limit, no visible error shown |
| S-hair | hair_groom_update.hlsl:36 and hair primary | P1/P2 | 32-bit wrap, non-atomic records | fixed in the hair session (bundle36) |
| T-F01..F20 | shader_identity, QA gates, asset builder, CTest registration, build.ps1 | P1/P2 | corrupt cache trusted, gates passing on empty data, ~170 unregistered tests, nondeterministic DB | fixed (bundle36): 172 CTests |

## 4. UI/UX review

Opus agent, 26 changes (docs/progress/cons-ui.md): Experimental shows only Water surface reflections (strand hair
hidden until its update, user decision), restart notes on every card, plain help texts with RTX 5090 costs,
Performance order, preset review dialog, Help & support (tested only on an RTX 5090, GPU choice), first-run game
folder notice, installer texts. No clipped text or wrapped buttons at 100/125/150/200%. Before/after screenshots:
`.claude/worktrees/cons-ui/.scratch/ui-review/` (not in Git). 167/167 tests at the time.
