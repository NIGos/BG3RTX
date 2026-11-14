# Launcher UI rework: proposal (2026-11-14)

Companion to `AUDIT.md`. Mockups: `mockups/index.html` (Direction A recommended, Direction B alternative).
No feature is added or removed; options that change behaviour are marked **Option**.

## 1. Information architecture (Direction A)

Navigation, fixed order: **Home, Lighting, Surfaces, Performance, DLSS 5, Experimental** | **Hotkeys,
Help & support**. Play and the version sit at the bottom of the sidebar on every page. The preset sits at the
top right of every page. Unsaved changes get a bar at the bottom of the window; nothing else lives there.

| Page | Group | Controls, in order (id) |
|---|---|---|
| Home | Status | Ready or blocked headline; "Needs attention" lists only problems and pending work |
| | Preparation | Shaders; RTX Hair styles with Pause/Resume/Try again; "Conversion options": Convert (Styles seen in game / All styles), granny2.dll, When (Game closed / Also while playing), last error |
| | This preset, In-game keys | Preset description and the changes from it; the three keys (read only, link to Hotkeys) |
| Lighting | Ray tracing | Ray-tracing detail (`rt_geometry`), DLSS Ray Reconstruction (`rr`), Mod lighting strength (`rt_intensity`) |
| | Shadows | Sun shadows (`sun_shadows`), Local light shadows (`local_shadows`) > Interior light shadows (`box_lights`) |
| | Bounced light | Indirect lighting (`gi`) > Off-screen bounce light (`exp_hidden_surface`) |
| | Hair | Hair shadows (`exp_native_coverage`), RTX Hair (`exp_hair_groom_primary`, Beta) > Detail at distance (`hair_distant_detail`), Motion stability (`hair_motion_stability`), conversion status line |
| Surfaces | Reflections, Water, Post effects | Reflections (`reflections`); Water lighting (`exp_water`); Depth of field and bloom smoothing (`rr_post_temporal`) |
| Performance | Frame rate | Frame Generation (`frame_generation`), NVIDIA Reflex (`reflex`), Rendered frame limit (`fps_cap`) |
| | Tracing speed | Faster bounce light tracing (`exp_gi_rt_pipeline`) > Bounce trace reordering (`exp_gi_rt_pipeline_reorder`) |
| DLSS 5 | DLSS 5 | DLSS 5 Neural Rendering (`dlss5_nr`), runtime file (Browse, automatic discovery, status) |
| Experimental | Switch, then groups | Show experimental options; Performance: Low-latency scene (`scene_handout`); Frame Generation: pacing test (`fg_destroy_wait_exact`), presentation test (`fg_present_metering`); Water: Water surface reflections (`water_surface_fresnel`); RTX Hair: Strand hair edge resolve (`exp_hair_strand_aa`) |
| Hotkeys | In-game keys | `hotkey_rt_toggle`, `hotkey_dlss5`, `hotkey_rtx_hair`: capture, Reset, Clear, rules |
| Help & support | Game and graphics | Game folder, GPU check, adapter choice when there are several |
| | Problem reports | Create report (same dialog), Detailed logging (`detailed_logging`) |
| | Tips, About | Overlay and comparison tips; version with Copy, mod files status, notices |

">" means a child row, indented under its parent and dimmed when the parent is off. A page that has
experimental options ends with one line that names them and links to Experimental. Developer mode keeps its
Advanced and Developer pages unchanged. Direction B maps the same controls onto four tabs (see the mockup).

**Options** (each needs a yes): (1) rename the first choice of Detail at distance from "Off" to "Full";
(2) drop "(experimental)" from the DLSS 5 label and use the badge; (3) rename "Low-latency scene" to
"Don't wait for scene build"; (4) show Tracing speed collapsed by default.

## 2. Interaction rules

1. **Presets and Custom.** The preset control is always visible. After any change it reads "Custom · N
   changes from Ultra · Reset to Ultra". A preset click keeps today's review dialog.
2. **Live or restart.** No badge means "applies when you save, also while the game runs". The only apply
   badge is amber: "Restart", or with a direction ("Restart to turn on"). After a save, rows that wait show
   a filled "Restart pending" badge, and a banner under the header names them and says whether the game is
   running. Launcher jobs (preparation) apply at once, sit outside the settings pages and never count as changes.
3. **Dependencies.** Decided at the click, shown at the control: "Also turned off: X, because it needs Y.
   Undo". Children whose parent is off keep their value and are dimmed, not reset. A choice whose needs are
   unmet stays clickable when the launcher can turn them on, and says what it will turn on. Save never
   changes anything the page has not already shown.
4. **Experimental.** One switch, on the Experimental page, above the options it reveals. "Experimental" is a
   violet badge; "Beta" (RTX Hair) is a teal badge for features that are off in presets but supported.
   Turning the switch off with options on asks first and lists what goes back to Off.
5. **Saving.** A changed row gets a gold edge, "Changed · was X" and its own undo. The bar reads "3 unsaved
   changes · 1 needs a game restart" with Review, Discard, Save. Save acts at once; Review is optional and
   groups the changes by "applies when you save" and "after a game restart", with dependent changes named.
   Play reads "Save & play" while there are changes. A blocked save names the blocker in the bar.
6. **Errors and empty states.** One red banner under the header for anything that stops Play, with its fix
   as a button; the same item heads "Needs attention" on Home, and Play shows the reason below it. Settings
   stay usable. A failed save keeps the edits and the bar. Empty states say what will appear and when
   ("No hair styles captured yet. The game captures each style the first time it shows it.").
7. **Long work.** One pattern: title, one status sentence, state badge (Ready, Preparing, Paused, Stopped),
   a bar with percent and time left, one action. Home's navigation item shows a dot while work runs.

## 3. Copy guidelines

- **Label**: the thing, at most four words, no status words, no parent name in a child ("Detail at distance").
- **Benefit line**: one sentence, at most 90 characters, what the player sees. No mechanism, no numbers.
- **Cost chip**: "≈ 0.13 ms", "≈ 0.6 ms Near · 3-4 ms Far", "saves ≈ 2 ms", "up to 520 MB video memory".
  Only measured or estimated values, from the `Cost` struct; unmeasured shows nothing on the row and "Not
  measured yet" in the detail. The reference GPU (RTX 5090) is said once, in the detail.
- **Detail on demand**, always the same five headings: What it does, GPU cost, Takes effect, Needs, Low ·
  Medium · High · Ultra. Scene names and dates stay in the docs.
- Plain words: "game restart", "when you save", "video memory", "turn on". Say what to do, not what failed
  ("Choose the folder that contains bin\bg3.exe"). One name per thing: "Create report", "game folder".

## 4. Visual system

- **Type** (Lato, Segoe UI fallback): 12 group heading (caps, +0.08em) and badges; 13 secondary; 14 body and
  labels (600); 16 card title; 20 page title; 22 Home headline. Line height 1.45.
- **Spacing**: 4, 8, 12, 16, 24. Row padding 12 x 16, group gap 24, page padding 20 x 24. Controls 28-32 px
  high, Play 44. Radius 6 (controls) and 8 (panels). Content column at most 860 px.
- **Colour tokens** (dark; contrast on `--surface` #171a1f): `--bg` #0f1114, `--surface-2` #1e2228,
  `--surface-3` #282d35, `--line` #2c3139, `--line-2` #626a78 (control borders, 3.2:1), `--text` #eee8da (14.3:1),
  `--text-2` #bcb6a7 (8.6:1), `--text-3` #948f83 (5.4:1), `--accent` #d9ae68 gold (8.5:1; ink #1b1408 on it
  8.9:1), `--info` #7fc4d6, `--ok` #8fc49a, `--warn` #f0b860, `--err` #f1948a, `--exp` #bba6f2. One meaning
  per colour: gold = selected, changed or primary action; amber = restart; red = blocks Play; violet =
  experimental; teal = links, focus, progress. A light variant uses the same tokens (in the mockups).
- **Components**: sidebar item; group (caps heading plus one panel); setting row (label, badges, benefit,
  meta line with restart badge, cost chip and key; control on the right; child variant); segmented choice;
  toggle with On/Off text; slider with value and optional Off/On; badges (Restart, Restart pending,
  Experimental, Beta, Ready/Preparing); cost chip; inline note (dependency); banner (error, restart);
  changes bar; dialog (review, preset, report); job row with progress; attention item; key capture field;
  detail panel (inline under the row below 1100 px).

## 5. Keyboard and screen readers

- Tab order: navigation, preset, page content top to bottom, changes bar, Play. Arrow keys move inside a
  segmented choice (roving tabindex, `role="radiogroup"`); Space/Enter toggles; Esc closes dialogs and
  cancels key capture; Ctrl+S saves; Enter on a row opens its detail.
- Toggles are `role="switch"` with the setting's name; rows link to their benefit, needs and note with
  `aria-describedby`; badges are text, never colour alone; the changes bar and dependency notes are
  `role="status"`; blocking banners are `role="alert"`; progress has `aria-valuetext` ("74%, about 35 min").
- Focus stays on the control after a re-render (kept from today); focus ring 2 px teal, offset 2 px.

## 6. Implementation plan (Direction A)

| Phase | What | Files | Risk |
|---|---|---|---|
| 1. Shell | Sidebar Play and version, preset in a fixed header, changes bar instead of the footer, banner slot | index.html, app.css, app.js `render`, `renderNavigation` | Low. Ids `play`, `save`, `discard`, `preset-N` stay |
| 2. Rows and groups | `card()` becomes `row()`; one `groups` table replaces `lightingOrder`/`performanceOrder`; child rows; detail panel fed by today's `details`, `requirements`, `apply` fields | app.js, app.css | Medium: the largest diff; control ids (`control-<id>-N`) stay so tests keep finding them |
| 3. Home, Hotkeys, Help | Home = status, attention, preparation, keys; conversion options collapse; Help regrouped; one report entry name | app.js `renderHome`, `hairPanel`, Support branch | Low |
| 4. Apply model | Restart badge with direction, "Restart pending", banner with names, per-row undo, "Save & play" | bridge: send `pendingRestart` ids, `gameRunning`, per-knob `saved` value; app.js | Medium: new state fields, covered by web_bridge_test |
| 5. Dependencies at the click | `set` runs `lower_dependents` and returns `adjusted` (id, from, to, reason); children carry `parent` | web_bridge.cpp `set`, settings.h (`parent` field or a UI group map), app.js note | Medium-high: touches the logic behind the 0.9.6 RTX Hair bug; add cases to web_bridge_test and settings_test |
| 6. Copy and cost | Short `desc`, cost from the `Cost` struct for release knobs, five detail headings; experimental page takes every knob whose non-default choices are all experimental | settings.h text (no key or ABI change), bridge sends `cost`/`saves`, USER_SETTINGS.md | Low, but many strings; settings_test checks metadata |
| 7. Experimental switch | Switch moves onto the page; confirmation before reset | app.js, bridge `experimental` preview | Low |

Phases 1 to 3 need no bridge or settings.h change and can ship first. No `common/host_api.h` change in any
phase; no new ini key; no shader work. Tests to update: `tests/launcher_ui_test.js` and
`tests/launcher_ui_test.py` (structure and ids), `tools/launcher/web_bridge_test.cpp` (phases 4, 5, 7),
`tests/settings_test.cpp` (page list near line 425, copy rules), `tools/launcher/ui_review.py` views,
`docs/USER_SETTINGS.md`, `docs/release/LAUNCHER_QA.md`. The basic window (`ui_product.h`) only takes the
new labels.

## 7. Questions before implementation

1. Direction A, B, or A with B's preset tiles on Home?
2. May labels and descriptions in `settings.h` be shortened as in the mockups (ids and keys unchanged)?
3. Which of the four Options in section 1 do you accept?
4. Dependencies at the click (phase 5) changes bridge behaviour: accept, or keep the Save-time dialog?
5. Should Save stay one click (Review optional), or always show the review when a restart is needed?
6. Is the light theme wanted, or dark only?
7. Should Low-latency scene move to the Experimental page (as drawn) or stay on Performance with a badge?
