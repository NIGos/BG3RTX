# Release defaults review — 2026-11-02

Stage 6, documentation and proposed diff only. Reviewed branch `codex-release-defaults`,
HEAD `855c99d`. No INI, code, preset, Git metadata or shared asset was changed. No build,
CMake, CTest, DXC, package operation or game was run. Shader request delta: **0**.

## Recommendation and load order

Keep the qualified Ultra features and every new experimental switch at its current value.
Remove the redundant quality overrides `ls_box=0` and `rt_emissive=0` from the release
profile; do not replace them with enabled values. Remove the five failed water experiments
in a separate implementation change. Memory qualification remains a release blocker.

Sources: [handoff](../HANDOFF_SESSION.md), [current progress and user decisions](../PROGRESS_CODEX.md),
[player README](README.md), [beta checklist](RELEASE_BETA.md),
[settings model](../../common/settings.h), [repository defaults](../../bg3rtx.ini),
[release profile](../../data/release.ini), [package script](../../scripts/package.ps1).

`scripts/package.ps1:122` concatenates `bg3rtx.ini` then `data/release.ini` into the
package's base INI. It does not select Ultra. `common/settings.h` applies a later
`bg3rtx_user.ini` layer, expanding its preset before applying explicit keys in that layer;
`BG3RTX_INI` is a further override. Consequently **release.ini is a base profile, not an
immutable restriction**. A player selecting Ultra gets `rt_emissive=1` and
`rt_hit_emission=1`. The old claim in [the November 1 review](../RELEASE_CHECK_20261101.md)
that release disables the emission control is obsolete. RELEASE_BETA already describes
the later GUI override correctly. Explicit user overrides can still change these values.

## 1. Complete difference table

Release means the assembled package base, before a user preset. Ultra means that base
with the current Ultra preset expanded, with no explicit user overrides. Repository is
the checked-in `bg3rtx.ini`. Numeric spellings such as `1` and `1.0` are equivalent.
The table includes every key differing between release and either comparison: **22 keys**.
All other keys agree. The only release-versus-repository value changes are `dev` and
`dump_shaders`; the other 43 release entries pin values already zero in the repository.
Preset choices, sliders and `kPresetExtra` were included, not just explicit release entries.

| Key | Repository | Ultra | Release base | Reason and assessment after today's merges |
|---|---:|---:|---:|---|
| `dev` | 1 | 0 | 0 | Hide developer tools and trace hotkey. Correct; Ultra does not enable developer mode. |
| `dump_shaders` | 1 | 0 | 0 | Avoid player shader dumps. Correct for every preset. |
| `fps_cap` | 0 | 58 | 0 | Ultra's rendered cap, 116 displayed with FG 2x. Correct; no cap in package base. |
| `gi_rr_rays` | 1 | 2 | 1 | Ultra uses two GI rays. Keep: one-ray energy passed, but flicker null did not qualify a reduction. |
| `rf_max_rough` | 0.6 | 0.8 | 0.6 | Ultra traces rougher reflections. Deliberate preset difference. |
| `rt_cascade_casters` | 0 | 1 | 0 | Qualified roof casters in Ultra; base avoids its cost. Keep. |
| `rt_rebuild_frames` | 60 | 600 | 60 | Ultra internal pacing for per-draw BLAS; static meshes are cached. Keep. |
| `rt_hit_material` | 0 | 1 | 0 | Ultra's qualified hidden-surface material bundle. Keep base off. |
| `rt_hit_material_instanced` | 0 | 1 | 0 | Required by that bundle. Keep; this does not enable IP grass. |
| `rt_hit_material_wind_rgb` | 0 | 1 | 0 | Qualified wind RGB rows in the bundle. Keep. |
| `gi_hit_direct` | 0 | 1 | 0 | Ultra + Bounce uses hidden-hit local lighting. Keep. |
| `gi_hit_sun` | 0 | 1 | 0 | Ultra + Bounce uses hidden-hit sun lighting. Keep. |
| `rt_hit_emission` | 0 | 1 | 0 | Qualified Hidden hits level. Keep; value 2 remains developer only. |
| `rt_emissive` | 0 | 1 | 0 | Capture prerequisite written by the emission control; +0.08 ms GI trace in m4p5. Release's explicit zero is redundant, not a runtime veto. Remove that line, retaining repository zero. |
| `gi_far` | 0 | 1 | 0 | Qualified 6 m Ultra bounce: S8 -2.01%, camp -2.42%; +3.2–3.7 ms GI trace. Keep. `gi_far_m=6` agrees everywhere. |
| `rt_water_geometry` | 0 | 1 | 0 | Ultra + Refraction chain; startup capture prerequisite. Keep. |
| `rt_water_reflections` | 0 | 1 | 0 | Qualified Ultra water reflections. Keep. |
| `rt_water_compose` | 0 | 1 | 0 | Qualified Ultra water composition. Keep. |
| `rt_water_shadows` | 0 | 1 | 0 | Qualified water sun shadows. Keep; separate from rejected flicker experiments. |
| `rt_water_ls_shadows` | 0 | 1 | 0 | Qualified water local-light shadows. Keep. |
| `rt_water_game_shadow` | 0 | 1 | 0 | Replace native water shadow where present. Keep. |
| `rt_water_refraction` | 0 | 1 | 0 | Qualified refraction in Ultra; whole water chain about 0.53–0.67 ms in progress. Keep. |

These are feature and cost differences, not grounds to turn the package base into Ultra.
High and lower presets must retain their own choices. Ultra's smoke result is a valid
functional/quality receipt, not evidence of long-session VRAM safety: median rendered
frame 16.65 ms versus 13.75 ms before emission and far field.

`ls_box` is absent from the difference table because all three values are **0**.
Keep Box light shadows Off in every preset until the user's S6 visual decision. Removing
its release override does not grant promotion. Measured cost was +0.32 ms S6 / +0.20 ms S7.
Likewise `rf_hit_shade=0` and `rf_hit_gi=0` agree everywhere: + Reflections remains
Experimental, with S8/S6/camp ratios 0.925/1.089/1.097 outside the accepted spread.

### Today's keys: no promotion

| Key or family | Reviewed default / Ultra | Release decision |
|---|---|---|
| `rt_vt_alpha_masks`, `rt_alpha_fine_lod` | 0 / 0 | Keep off. Catalogue availability is not a passed brightness gate; masks are generated on the player's PC, not shipped. |
| `rt_omm`, `rt_omm_reference` | 0 / 0 | Keep off; reference is diagnostic. Forced-opaque upper bound is not OMM qualification. |
| `rt_omm_dyn` and other incoming OMM switches | Not present at this HEAD; progress names dynamic bake awaiting om4 | Incoming keys must stay off. Do not infer enabled runtime OMM from catalogue coverage (~97%) or the 5.7–6.3 ms upper bound. |
| `rt_hit_material_ip_grass` | 0 / 0 | Keep off: +1.9 ms, +1.5 GB for about +3 points share, per current progress. |
| `rt_hit_material_generated` | 0 / 0 | Keep off: current progress records only +2.3 points camp; corrected bush/two-sided rerun is pending. |
| `rt_water_rr_mv`, `rt_water_fg_mv`, `rt_water_glint_aa`, `rt_water_rf_edge`, `rt_water_rr_hitdist` | 0 / 0 | Remove failed experiments as below; retain zero until removal. |
| `rt_spec_cascade` | Absent from this HEAD's defaults and settings | User steering requires off. Migration is pending in the handoff; do not add an enabled release override. Reconcile the final migration interface before packaging. |
| `gi_temporal_mv_sample`, `gi_prev_radiance_guard` | Absent at this HEAD; both 0 in `git show variant-migration:bg3rtx.ini` | Keep incoming guard switches off. Progress says live steering approved, not gates passed. |

The branch snapshot predates parts of the ongoing migration. This is an explicit review
limit, not a claim to have reviewed unmerged implementations. The unconditional NaN/Inf
repair is different: [recover-gi-history-nan](../progress/recover-gi-history-nan.md)
calls it a pure fix with **no key**. Keep that repair; never disable it by treating it as
one of the experimental reprojection guards.

## 2. Removal candidates and GUI boundary

Remove these five public keys and their unused implementation paths before release,
preserving archived evidence. Removing only their release overrides would not remove
them: package.ps1 still ships the repository INI and shaders. This review changes neither.

| Key | Evidence and disposition |
|---|---|
| `rt_water_rf_edge` | **Rejected.** wf2 water flicker 3.29% versus 2.25/2.32% OFF (+42%). [wf2 report](../progress/worktree-agent-a81baa03b972b117a.md), [wf3 verdict](../progress/wf3-nocompose.md); repository INI explicitly labels REJECTED. |
| `rt_water_rr_hitdist` | **Null.** wf2 2.33% versus 2.25/2.32% OFF; earlier apparent -25% was confounded. Same wf2/wf3 sources; INI labels NULL. |
| `rt_water_rr_mv` | **Null.** wf4 3.31% versus 3.38/3.38% OFF, recorded in repository INI; [wf4 design](../progress/wf4-rr-water.md) is an older READY_FOR_LIVE fragment, superseded by current progress and the [wf5–wf7 account](../progress/wf5-fg-water.md). |
| `rt_water_fg_mv` | **Null.** valid wf6, 8/8 gates: water parity 1.11 versus rest 1.12, unchanged from OFF. [wf5–wf7 account](../progress/wf5-fg-water.md). |
| `rt_water_glint_aa` | **Null observed benefit, extra cost.** wf5 peaks 5.52% versus 5.31/5.26%, +0.09 ms compose. wf5 presented captures were INVALID, so this is not a valid FG receipt; later attribution plus current progress closes the flicker hypothesis. Same report. |

[Current progress](../PROGRESS_CODEX.md), Report 19:30, rejects every water flicker key;
[handoff](../HANDOFF_SESSION.md), Next 3, explicitly requests their removal. The final
wf7 summary attributes the effect to native water: RT 0.54 versus 0.77–0.86. Retain the
qualified water chain; it is not one of these failed interventions.

IP grass and generated rows are **not qualified**, but do not have the same removal
verdict: generated rows have a concrete corrected rerun pending. Keep them off and out
of the player GUI rather than claiming they are permanently rejected. OMM, VT masks,
fine LOD and reprojection guards likewise remain pending. `gi_rr_rays=1` is not a null
key to delete: [rays-flicker-rerun](../progress/rays-flicker-rerun.md) passed energy
(+0.11%, band 1.47%) but failed its OFF/OFF flicker control; Ultra remains 2 rays.

Developer-only controls must remain hidden with `dev=0`:

- `rt_hit_reference` (lighting reference), `ls_k` with `ls_rest_samples`, and
  `rt_sun_cascade_current`: the three `dev_only` knobs in `kKnobs`.
- `rt_hit_emission=2`: a developer choice inside the player emission control. Values
  0 and 1 stay visible. The user has not chosen to promote 2.
- Every diagnostic pinned in release.ini: shader/census/geometry dumps and timings,
  GI/RT/LS/RF views and statistics, native-direct validation, RF validation, RR checks,
  raw dumps/failure injection, emissive overlap/logging, primary checks/epoch/identity/
  fade class, CPU timing, crash dumps, pan/replay/mesh captures, order/stability/capture/
  draw logs, SHaRC debug/stats/leak/fine checks, `ls_check`, and hit reference.
- Raw A/B and attribution controls including `rt_omm_reference`, fine LOD, IP grass,
  generated rows, spec migration switches, GI guard switches and the five water
  experiments: no new player controls while unqualified. Existing automatic fixes
  (recycle/unlocked/lean hooks, scene wait, letterbox, pool alpha) stay automatic.

`rt_mesh_audits=2`, `rt_em_calib=1` and sliced `rt_em_check_*` are correctness mechanisms,
not failed debug features to strip. Keep player A/B `ab_hotkey=1`. Crash dumps remain
off by the explicit user decision; a future report mode is separate work. Experimental
Native coverage, hair origins, OMM, offline cutout masks and capture spill are currently
visible by design, not developer-only knobs accidentally exposed. Do not silently
hide qualified Advanced water, hidden shading, emission or far field. The five GUI
audit decisions remain open; the referenced `GUI_AUDIT_20261102.md` is absent at this HEAD.

## 3. VRAM risk by preset and card capacity

Read the sibling audit exactly through
`git show codex-vram-audit:docs/VRAM_AUDIT.md` (source revision `855c99d`). It is not
copied here or assumed merged. Its numbers are MiB payload/capacity estimates unless
otherwise marked, not a complete resident heap ledger. It includes game, opaque driver
objects and NGX/Streamline uncertainty. No defensible total per preset exists yet.

**Observed failure:** S8 port long view reached **31.7 GB against a 30.6 GB budget**
on a nominal 32 GB card: **1.1 GB over budget**, about 3.6%. This is the user's reported
event, not a newly reproduced measurement. Nominal capacity is not the Vulkan budget.
The roughly 22 to 31.7 GB session growth is not fully attributed. Today's source trim
saved about 0.4 GB; subtracting it from that peak would still not prove safety.

The following is a relative release-risk assessment for the same demanding scene and
4K-class DLSS Quality use, not measured minimum requirements or an FPS recommendation.
Every cell still needs multi-hour scene-transition testing. Lowering a preset live may
leave allocations retained: assess a fresh launch as well as long-session growth.

| Preset | 12 GB | 16 GB | 24 GB | 32 GB |
|---|---|---|---|---|
| Low | Lowest BG3RTX memory risk; only provisional starting point | Lowest relative risk | Lowest relative risk | Lowest relative risk |
| Medium | High risk; opaque RT, RR/FG and scene reserves remain | Elevated risk; unmeasured | Lower relative risk; unqualified | More headroom; unqualified |
| High | Very high risk; do not advertise as qualified | High risk; full alpha scene and pools | Elevated risk; no long-session receipt | Elevated retention risk; unqualified |
| Ultra | Extreme risk; do not recommend | Extreme risk; do not recommend | Very high risk; do not recommend | **Demonstrated over-budget risk** in S8 long view; not memory-qualified |

Low disables scene build and RR, avoiding major RT allocations at startup, but game,
screen-space images and FG can still consume memory. Medium omits alpha-tested geometry
and Ultra extras; opaque geometry still needs capture, mesh/AS storage and RR. High
adds full alpha geometry. Ultra adds roof casters, hidden-material streams, emissive
capture and water records. Extra rays/farther rays primarily affect GPU work and must
not be converted into an invented linear VRAM multiplier.

Material risks identified by the audit:

- Mesh pool cap 2047 MiB, observed 1255 MiB in S8: 792 MiB slack in that window.
  It is capacity, not another allocation to add on top of pool storage.
- AS allocation chunks are 64 MiB minimum. S8: 29 chunks, at least 1856 MiB committed
  against 267.1 MiB live AS. Historical camp: 66 chunks, at least 4224 MiB against
  255.4 MiB live. Free space/fragmentation can persist; do not add live AS again.
- Cascade reserve 427 to 544 MiB costs 351 MiB over three slots, not 117 MiB total.
  Scene demand and safe retirement must decide any shrink.
- Hit-material streams overlap source slots, deferred packets and query bases;
  a 768 MiB basis cap is not the whole consumer. Historical experimental IP grass
  reached 3296 MiB streams before trimming; it is not a present Ultra total.
- Water compose example: 112-byte records and 560 MiB configured capacity, logged
  559 MiB payload. The older GUI/progress “up to 520 MB” describes a different
  record configuration and should not be treated as a current bound. Emission
  soup can grow to 512 MiB and retain its high-water capacity.
- Resolution-dependent images and SDK memory are additional. One FG MV/depth copy
  slot costs 20.84 MiB at 2560x1067, 28.13 MiB at 2560x1440; up to eight lazy slots.
  Deferred destruction can temporarily retain both old and replacement resources.

The launcher's `suggest_preset` chooses by GPU name/series/tier and laptop status,
not available memory budget. Thus the README hardware examples are provisional
performance guidance, not capacity guarantees (especially model variants with different
VRAM). Before release, reconcile those examples with measured memory and the eventual
budget guard. Do not lower capture limits speculatively: overflow can lose geometry.
Audit savings proposals are not implemented or additive guarantees. Long runs and
lower-VRAM hardware qualification are still pending; no live batch is requested here.

## 4. Proposed release-INI-only patch

Proposal only. This removes two redundant quality pins without changing any effective
value today. It leaves all diagnostics at zero, every experimental default off, and the
user's pending decisions intact. Do not enable emission globally: its prerequisite and
consumer must be selected together through the existing Ultra/player control.

```diff
diff --git a/data/release.ini b/data/release.ini
--- a/data/release.ini
+++ b/data/release.ini
@@ -3,6 +3,8 @@
 # scripts/package.ps1 appends this to bg3rtx.ini for a player package; later
 # lines win, so every developer/debug key below is off. The repository's own
 # bg3rtx.ini (developer workflow) is unchanged.
+# Quality keys stay in bg3rtx.ini and the later player preset/user layer.
+# In particular, Ultra enables rt_emissive with rt_hit_emission=1.
 dev=0
 dump_shaders=0
 shader_debug=0
@@ -16,7 +18,6 @@
 rt_stats=0
 ls_debug=0
 ls_stats=0
-ls_box=0
 ls_native_direct_validate=0
 rf_debug=0
 rf_stats=0
@@ -24,7 +25,6 @@
 rr_post_check=0
 dump_raw=0
 rr_test_fail=0
-rt_emissive=0
 rt_em_overlap_check=0
 # Diagnostics bg3rtx.ini already has at 0, pinned off here so a developer edit of
 # bg3rtx.ini cannot ship them (docs/RELEASE_CHECK_20261101.md). rt_primary_check
```

Applying this later also requires regenerating `kReleaseDefaults` with the existing
generator and the normal settings validation; those are outside this docs-only task.
The fenced patch intentionally touches only release.ini. Do not interpret this small
cleanup as completion of rejected-key removal, memory work, install qualification,
licensing or the paused packaging decision.

## Review verification and remaining limits

Read-only parsing compared the repository and release assignments with the checked-in
schema, cross-checked against `kKnobs`, slider scale and `kPresetExtra` in settings.h.
Result: 45 release pins; 2 values differ from repository; 20 Ultra quality/cost values
differ from package base. No compiler or test runner was invoked. The patch is a proposal,
not applied. Git metadata remains untouched and no commit was created.

Before packaging resumes: review the final migration HEAD, keep all incoming keys off,
remove the five failed experiments, refresh stale README/GUI memory claims, resolve
the existing user decisions, and obtain memory/long-session qualification. This document
does not certify pending features or any card capacity as release-safe.
