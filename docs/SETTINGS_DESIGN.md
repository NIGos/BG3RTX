# User-facing settings: design study

Status: **phase 1 implemented (2026-10-26)**: settings table, presets, layered ini, launcher,
release profile, `fps_cap`. See §9 for what is done, what differs from this study, and what
is still open. Scope: how BG3RTX exposes its ray-tracing settings to players with weaker PCs.
It covers presets, a short list of exposed knobs, where the menu lives, and how changes apply
while the game runs.

Sources: the code (`layer/`, `core/`, `bg3rtx.ini`), the track docs (costs) and the web
sources cited inline. Every cost figure says whether it was **measured** (RTX 5090, render
2560x1067 → output 3840x1600, from `TRACK_*.md`) or **extrapolated** (my estimate, to be
measured).

---

## 0. Summary

- **Recommended approach.** Settings are drawn by **our own Dear ImGui overlay inside the
  layer**, opened with a hotkey. A **small launcher/config exe** comes with it; distribution
  needs one anyway, because the layer is enabled through environment variables
  (`scripts/launch-bg3.ps1` sets `VK_ADD_LAYER_PATH` / `VK_INSTANCE_LAYERS`). The launcher is
  also the **fallback** UI and the first thing to ship. An MCM bridge for players who already
  run the Script Extender is an optional extra, never a requirement.
- **Why not the game's own Options menu?** Editing Larian's XAML pages cannot store or read
  new values without the Script Extender. It also conflicts with every other UI mod that
  replaces the same page (§2.3).
- **Presets.** Low / Medium / High / Ultra (5090). Low turns the hardware scene build off: on
  weak GPUs the build (≈1.9 ms static on a 5090) dominates the cost, not the effects.
- **Exposed knobs.** 10 settings, plus the game's own DLSS mode, which stays in the game's
  menu (§6).
- **Code changes the settings layer needs first:**
  - "effective RR": today the GI, local-shadow and reflection paths follow the `rr` *key*,
    not whether RR actually runs;
  - automatic `rt_build`;
  - a user ini layered over the defaults;
  - capability detection;
  - release defaults for the dev keys.

---

## 1. Inventory of every setting the code reads

How settings work today:
- `cfg_poll()` (`layer/util.cpp`) re-reads `bg3rtx.ini`, or `%BG3RTX_INI%`, on every present
  when the file's mtime changes.
- `cfg_int` / `cfg_float` are map lookups under a mutex, called per frame.
- The core reads keys through `host->cfg_*`.
- A few keys are read once (`static const`, or at instance/device creation).

**Classes:**
- **U**: user quality/performance knob.
- **A**: artistic intensity.
- **D**: debug/dev only.
- **N**: never expose. Changing it breaks capture or compatibility, or it is a
  reverse-engineered constant.

**Apply:**
- **L**: live, next frame.
- **L\***: live, but resets history or reallocates automatically.
- **R**: restart (read at instance/device creation, or static).

### 1.1 Feature switches and quality (class U)

| Key | Default (ini) | Apply | Cost (5090, measured unless noted) | Notes / dependencies |
|---|---|---|---|---|
| `rr` | 1 | L* | RR 2.6 ms vs SR 0.8 ms, so **+1.8 ms** | Needs the game's upscaler on **DLSS** (`ResolutionUpscaleType=3`), because it replaces the SR evaluate. `gi_rr`, `ls_soft` and `rf_rr` follow it; switching reallocates the GI images (full res ↔ half res, `gi_res_shift`) |
| `fg` | 2 | R for 0↔2+, L for 2/3/4/0 once Streamline is loaded | DLSS-G itself ≈ 1 ms at this output (extrapolated); 2x ≈ 103 → 206 fps measured | RTX 40+ for 2x, RTX 50 for 3x/4x. Headless mode refuses `fg>=2` |
| `reflex` | 1 | L | ~0 | 0/1/2 (boost). Markers are always sent |
| `rt` | 1 | R | device features only | `VK_KHR_acceleration_structure` + `ray_query`. Must stay 1 on capable GPUs so that every RT knob below can switch live |
| `rt_build` | 1 | L | XFB +0.60, convert 0.40 (stride 24), BLAS refit 0.88 / rebuild 3.4, TLAS 0.02. Total **≈1.9 ms static, ≈4.4 ms when the camera pans** | **Not tied to the effects today**: with every RT effect off the scene still builds. Must become derived (§7.3) |
| `rt_alpha` | 1 | R | refit 0.39 → 0.88 ms, +64 MB | Alpha-tested casters (grates, foliage). The candidate restart-only "quality" knob |
| `rt_alpha_test` | 1 | L | shadows + AO 0.20 → 0.22 ms | Live A/B for the above. Internal |
| `rt_shadows` | 1 | L | 0.08–0.11 ms | Sun. Without RT, contact shadows (`contact_shadows`) are the fallback |
| `gi` | 1 | L | GI total 0.35 ms (`gi_rt`) / 0.44 ms (screen-space + `rt_ao`) | Also the host of `rt_ao`, `gi_rt` and the bounce source for `rt_reflections` |
| `gi_rt` | 1 | L | trace 0.175 ms (vs 0.248 ms screen-space) | Needs `rt`, `gi` |
| `gi_rays` / `gi_rr_rays` | 4 / 1 | L | 2 rays/12 steps 0.18 ms, 4/16 0.28 ms (half res, `rr=0`); RR path 1 ray full res ≈ the same | The RR mode uses `gi_rr_rays` |
| `gi_steps` | 16 | L | see above | Screen-space path only |
| `rt_ao`, `rt_ao_rays`, `rt_ao_radius` | 0 / 2 / 1.0 | L | +0.13 ms | Superseded by `gi_rt` (only used if `gi_rt=0`) |
| `rt_local_shadows` | 1 | L | **0.345 ms** foundry, 0.068 ms docks | Needs the light list bindings, `rt` |
| `ls_samples` | 0 | L | 0 → 0.345 ms; 1 (RIS) → **0.219 ms** | RIS is the cheaper "Medium" mode, but noisier: needs RR |
| `ls_shadowed` | 0 | L | unmeasured (no scene with shadow-casting lights yet) | 1 replaces the game's atlas (loses off-screen casters); 2 takes the min of both |
| `ls_soft` | -1 | L | 0.337 vs 0.345 ms | Follows `rr`. Hard shadows without a denoiser |
| `rt_reflections` | 1 | L | **0.33 ms** foundry / 0.13 ms docks | Needs `rt`, `gi` (`prevRadiance`), RR for glossy rays |
| `rf_max_rough` | 0.6 | L | scales with the fraction of traced pixels (extrapolated: 0.8 → ≈0.45 ms) | `rf_max_rough_norr`=0.3 without RR (mirror rays only) |
| `rr_spec_hit_distance` | 1 | L | ~0 | Internal: must follow `rt_reflections` |

### 1.2 Artistic intensity (class A)

| Key | Default | Notes |
|---|---|---|
| `gi_strength` | 1.0 | bounce intensity |
| `gi_ao` | 1.0 | weight of the RT visibility in place of the game's SSAO |
| `rt_strength` | 1.0 | sun shadow fold |
| `ls_strength` | 1.0 | local shadow delta |
| `rf_strength` | 1.0 | reflection delta |
| `cs_strength`, `cs_length`, `cs_steps` | 1.0 / 0.02 / 12 | contact shadows (non-RT fallback) |
| `ls_source_radius` | 0.05 | penumbra size (soft path) |
| `rr_preset` | 0 | 0 default, 4 D, 5 E. Look rather than cost |

Rule: at most **one** intensity slider reaches users, "RT lighting intensity"
(§6, knob 9), which scales `gi_strength`, `ls_strength` and `rf_strength` together within
0.5–1.0. The individual keys stay ini-only for tinkerers. Values above 1.0 break the
"never brighter than the game" property of the exact deltas (`QUALITY.md` R3), so the slider
is capped at 1.0.

### 1.3 Internal tuning (class N: correctness constants, never exposed)

- **Ray geometry / bias:** `rt_bias`, `rt_tmax`, `rf_tmax`, `rf_depth_k`, `rf_edge`,
  `rf_mirror_rough`, `rf_models`, `gi_radius`, `gi_thickness`, `gi_history`,
  `gi_rt_radius`, `gi_rt_depth_k`, `gi_rt_fallback`, `gi_reproject`, `ls_clear`,
  `ls_min_radius`, `rt_sun_radius`.
  - Note: `rt_sun_radius` is read but unused by the hard-shadow path today.
  - `rt_tmax`, `rf_tmax` and `gi_rt_radius` are where "longer rays" would go. §5.5 explains
    why they stay internal.
- **Mode routing:** `gi_rr`, `rf_rr`, `ls_soft` (-1 follows `rr`), `rf_max_rough_norr`,
  `rr_guides_v2`, `rr_guides_at`, `rr_nov`.
- **BVH policy:** `rt_refit`, `rt_rebuild_frames` (60). (`rt_fast_trace`, no measured gain: removed 2026-10-31, see CONSOLIDATION.md C1.)
- **Memory budgets** (R): `rt_xfb_mb` 384, `rt_alpha_mb` 64, `geo_xfb_mb`, `geo_max_draws`,
  `geo_indirect_mb`.
- **Frame-gen plumbing:**
  - `fg_mvec_*`, `fg_jitter*`, `fg_near/far/fov`, `fg_camera`, `fg_cam_binding`,
    `fg_prev_binding`;
  - `fg_tag_*`, `fg_lifecycle`, `fg_menu_detect`, `fg_ui_recompose` (wrong for semi-transparent
    UI, off), `fg_only_interpolated`, `fg_stable_frames`, `fg_game_ngx`, `fg_native_of`,
    `fg_watch_game_ngx`;
  - `sl_*` (`sl_init_at`, `sl_redirect_gipa`, `sl_patch_plugins`, `sl_veh`, `sl_dev`,
    `sl_app_id`).
- **Hooks / compatibility** (R, static): `ngx_hook`, `core`, `rt_bda_core`,
  `force_transfer_src`, `force_storage_rgb10a2`, `force_storage_r8`, `force_present_mode`,
  `rt_alpha_capture`, `geo_capture`, `headless`. (`geo_xfb_always`: removed 2026-10-31, see CONSOLIDATION.md C1.)

### 1.4 Debug / dev (class D: stripped from the shipped ini, defaults off)

- **Debug views:** `rr_show`, `gi_debug`, `rt_debug`, `ls_debug`, `rf_debug`, `rf_validate`.
- **Stats:** `rt_stats`, `ls_stats`, `rf_stats`. These cost **≈4 ms** each, so they must never
  be on in release.
- **Timing logs:** `geo_timing*`, `rt_timing_frames`, `gpu_timestamp_period_ns`.
- **Logging:** `*_log_*`, `log_*`, `fg_state_every`, `sl_verbose`, `sl_veh_max`,
  `sl_log_barriers`, `ngx_log_dlls`, `rr_log_camera`, `fg_log_camera`, `fg_log_presents`.
- **Snapshots and describes:** `fg_snap*`, `gi_describe*`, `census_dump`.
- **Captures and dumps:** `geo_obj`, `geo_patch_dump`, `rr_dump_gbuffer`, `dump_*`,
  `auto_*`, `watchdog_s`, `shader_debug`.
- **`dump_shaders=1` is the default today.** It writes every SPIR-V module to `captures/`,
  so a release build must default it to 0.
- **The tracer hotkey** `Ctrl+Shift+F10` (`GetAsyncKeyState`) should be gated behind a
  `dev=1` key in release builds. Otherwise a user can fill the disk with traces.

---

## 2. In-game menu integration options

BG3's UI is NoesisGUI (XAML), shipped in the `.pak` archives ([Noesis](https://noesisengine.com),
[GameFromScratch overview](https://gamefromscratch.com/noesisgui-hands-on-with-the-game-user-interface-framework/)).
The install on this machine has `bin/SDL2.dll` (the window and input layer) and no
`DWrite.dll`, so the Script Extender is **not** installed here.

### 2.1 Script Extender (Norbyte's BG3SE), Lua + IMGUI

- **What it offers.**
  - Lua in client and server contexts.
  - `Ext.IMGUI` windows (`Ext.IMGUI.NewWindow("...")`;
    [community IMGUI tutorial](https://wiki.bg3.community/Tutorials/ScriptExtender/ImGui-and-You/Dear-ImGui)).
  - Noesis access through `Ext.UI` (accessed via `Ext.UI.Defer()` since SE v33).
  - `Ext.IO.SaveFile`, which writes under
    `%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender`
    ([bg3se API.md](https://github.com/Norbyte/bg3se/blob/main/Docs/API.md)).
- **How SE draws IMGUI on Vulkan.** It detours the loader's `vkCreateInstance`,
  `vkCreateDevice`, `vkCreateSwapchainKHR` and `vkQueuePresentKHR` with Microsoft Detours.
  On present it records an ImGui render pass into the acquired swapchain image and rewrites
  the game's present wait semaphore to its own
  ([bg3se `IMGUI/Vulkan.inl`](https://github.com/Norbyte/bg3se/blob/main/BG3Extender/Extender/Client/IMGUI/Vulkan.inl)).
  Consequences for us:
  - Its hooks sit **above** our layer. It draws into the image the game hands to
    `vkQueuePresentKHR`; with `fg>=2` that present is routed to Streamline's proxy.
  - DLSS-G therefore sees an SE window baked into the back buffer but **absent from the
    UI-colour tag**. While the window is open, interpolated frames may warp it. This is
    unverified, but it is the same class of problem that OptiScaler's "HUDfix" exists for
    ([OptiScaler README](https://github.com/optiscaler/OptiScaler)).
  - Two parties then rewrite present semaphores on one swapchain. That is a new crash
    surface, and the headless test setup never exercises it.
- **How values reach our DLL.** A Lua mod writes `Script Extender/BG3RTX/user.ini` with
  `Ext.IO.SaveFile`. The layer's `cfg_poll` already watches an mtime; it would watch a second
  path. There is no need for a pipe or shared memory: a 1 s poll latency is fine for settings.
- **Dependency for users.** The SE DLL (`DWrite.dll` in `bin/`) plus our Lua mod. SE's own
  loader updates itself; the game is frozen, so SE's address mappings no longer move.
- **Maintenance risk.** Low on the game side (frozen), medium on the SE side (its Vulkan
  hooks change independently of us).

### 2.2 Mod Configuration Menu (MCM, by Volitio / AtilioA)

- **What it offers.**
  - A centralised in-game settings UI built on SE IMGUI: checkboxes, sliders, combos,
    radio buttons, keybindings and more.
  - Mods declare their options in an `MCM_blueprint.json` next to `meta.lsx`
    ([community wiki](https://wiki.bg3.community/Tutorials/Mod-Frameworks/mod-configuration-menu)).
  - Opened with **Insert** or from the ESC menu.
  - Settings are kept in JSON per profile, and profiles can be exported
    ([BG3-MCM README](https://github.com/AtilioA/BG3-MCM),
    [Nexus page](https://www.nexusmods.com/baldursgate3/mods/9162)).
- **Fit.** It is good: our blueprint would be about 10 entries plus a preset combo, and a
  20-line Lua listener would mirror changes into `user.ini`.
- **Costs.**
  - **Dependencies:** SE **and** MCM **and** our pak mod, which must be enabled in the mod
    manager.
  - **IMGUI and DLSS-G:** inherits everything in §2.1.
  - **Hotkey clash:** Insert is also OptiScaler's default overlay key.
- **Verdict.** A good *optional* bridge (Phase 3) for the large share of players who already
  run SE + MCM, never the only route.

### 2.3 Editing the game's XAML options page via a pak mod

- **Mechanics.** Override the Options page's `.xaml` in a pak (loose-file or pak UI mods
  like Aether's replace whole pages:
  [example](https://nexusmods.com/baldursgate3/mods/1279)).
- **What fails.**
  - **No backing data.** New controls bind to view-model properties that Larian's C++ does
    not have. Without SE (`Ext.UI` Noesis view-models) a slider can be drawn but its value
    is never stored or read. With SE, this is §2.1 plus fragile XAML.
  - **Conflicts.** The Nexus pages themselves warn that mods replacing the same XAML file
    conflict ("only install ONE version"). A Noesis-native options framework exists
    ([NMCM](https://www.nexusmods.com/baldursgate3/mods/24342), Patch 8+). It is another
    dependency and states it does nothing alone.
  - **Value transport.** Still a file written by SE.
- **Verdict. Not feasible without SE**, highest maintenance and conflict risk. The frozen
  game removes the "patch breaks XAML" risk but not the mod-conflict risk. Rejected.

### 2.4 Comparison

| Route | Feels native | Extra user dependency | Transport | FG interplay | Risk |
|---|---|---|---|---|---|
| XAML options page | best | SE (+ framework) | SE file | via game UI tag, fine | high (conflicts, bindings) |
| SE IMGUI / MCM | good (Insert, ESC menu) | SE + MCM + pak | SE file | **not in UI tag** | medium |
| Own overlay (§3) | good | none | in-process | we control it | medium (input) |
| External launcher (§4) | none | none (ships with us) | `user.ini` | none | low |

---

## 3. Our own overlay (Dear ImGui on Vulkan inside the layer)

This is the ReShade / Special K / OptiScaler model. We already own the device, the
swapchain path (`layer.cpp` `CreateSwapchainKHR`/`QueuePresentKHR`) and the window handle
(`sl_note_surface` records the HWND from `vkCreateWin32SurfaceKHR`).

### 3.1 Drawing

- **Where.** In our `QueuePresentKHR` hook, **after** the game's UI composite and **before**
  the call into Streamline's present proxy:
  - one extra command buffer on the present queue;
  - it waits on the game's semaphores, renders ImGui into the swapchain image and signals
    our own semaphore;
  - the present waits on that semaphore.
  This is the same pattern as bg3se, but inside the layer, so the order against Streamline
  is ours.
- **HDR10.** The swapchain is RGB10A2 PQ with HDR10 (`TRACK_B_FG.md`). ImGui emits sRGB
  colours, so the overlay pipeline's fragment shader must PQ-encode at a fixed paper white
  (~200 nits). For SDR (B8G8R8A8) it is a plain sRGB pass. Both variants come from one HLSL
  file through our DXC path. The ImGui Vulkan backend's stock shaders are not used.
- **Frame generation.** While the menu is open, **set DLSS-G mode to off** (live; `fg` 2/3/4/0
  already switch once Streamline is loaded) and restore it on close.
  - The overlay is then never interpolated.
  - No UI-tag tricks are needed. Drawing into the tagged UI buffer would need the game's
    linear-light UI blending, which `fg_ui_recompose` already showed is wrong for
    semi-transparent UI.
  - The visible drop to the rendered frame rate is acceptable in a settings menu, and it
    shows the user the base frame rate their choices produce.
  - The menu shows a live line with base fps, displayed fps and our GPU ms; the passes are
    already timestamped.
- **HUD-less buffers.** The overlay goes into the back buffer only, never into the hudless
  tag. Screenshots via the tracer must skip the overlay pass. When the menu is closed the
  overlay records nothing at all.
- **Headless mode.** `headless=1` fakes acquire/present. The overlay is disabled whenever
  headless is on or `dev_noui=1`, so `auto-run.ps1` and the scene sweeps never see it.
- **Cost.** About 0.05–0.1 ms while open (extrapolated); zero closed.

### 3.2 Input

- **The window.** It is SDL2's. Subclass the HWND with `SetWindowLongPtrW(GWLP_WNDPROC)` the
  first time the swapchain is created; we have the HWND from the surface.
- **While the menu is open:**
  - feed `WM_MOUSE*`, `WM_KEY*` and `WM_CHAR` to `ImGui_ImplWin32_WndProcHandler` and swallow
    them;
  - swallow `WM_INPUT` too: SDL uses raw input in relative-mouse mode.
- **Unverified:** whether BG3 polls raw input in a way that bypasses the window procedure.
  Check it on the first prototype; the fallback is `ClipCursor` plus a forced visible cursor.
- **Controllers.** Gamepads (SDL GameController / XInput, polled) cannot be blocked this way.
  The overlay is a mouse-and-keyboard UI, and the game is paused anyway while the in-game
  ESC menu is open.
- **The hotkey.** Polled with `GetAsyncKeyState`, like the tracer. The default is
  **Ctrl+Shift+F9**, configurable (`menu_key=`). Keys to avoid:
  - Insert (MCM, OptiScaler);
  - Home (ReShade);
  - F12 (Steam screenshots);
  - Alt+F9/F10 (NVIDIA ShadowPlay);
  - Ctrl+Shift+F10 (our tracer).

### 3.3 Menu layout

The menu has one window:
1. the preset combo;
2. the 10 knobs (§6), each with a "restart needed" or "brief hitch" badge where §7 says so;
3. GPU capability lines (RR / FG / MFG available);
4. the live timings;
5. "Reset to preset" and "Save".

Saving writes `bg3rtx_user.ini` atomically (temp file + `MoveFileEx`), and the existing
mtime poll applies it on the next present.

---

## 4. External launcher / config exe

A launcher is **needed anyway** for distribution: the layer is enabled through
`VK_ADD_LAYER_PATH` + `VK_INSTANCE_LAYERS` set before `bg3.exe` starts
(`scripts/launch-bg3.ps1`), and players should not run PowerShell. That makes it the cheapest
first UI.

- **What it does:**
  1. Detects the GPU: vendor, name, RT support. It creates a VkInstance and reads
     `VkPhysicalDeviceRayQueryFeaturesKHR` and the vendor/device ID.
  2. Proposes a preset by GPU class (§5.1).
  3. Edits `bg3rtx_user.ini`.
  4. Optionally sets the game's upscaler in `graphicSettings.lsx` (`ResolutionUpscaleType`,
     `DLSS2Mode`), the same two entries `scripts/ensure-settings.py` already rewrites, so a
     preset can include the DLSS mode.
  5. Launches `bg3.exe` with the layer environment.
  It works while the game runs too: the ini hot-reloads.
- **Toolkit:** a **single Win32 + Dear ImGui (DX11) exe.**
  - It shares the knob table (one header: key, label, range, per-preset values) with the
    overlay, so the two UIs cannot drift.
  - It is about 1 MB with no runtime.
  - WinUI 3 needs the Windows App SDK runtime; Avalonia needs .NET. Both are heavier for a
    15-control window and bring a second language into the repo.
- **Limits.** It has no live preview, and it does not cover players who launch from Steam.
  For those, the shipped README shows the Steam launch option
  `"<path>\BG3RTX.exe" %command%`. With that option the launcher sets the environment and
  starts the game itself.

---

## 5. Presets

### 5.1 Target classes and budgets

The budget is BG3RTX's **added** GPU time per rendered frame over the vanilla game at the
same DLSS mode. RR's delta over SR is included.

| Preset | Target GPU class | Typical output / game DLSS mode | Added budget |
|---|---|---|---|
| **Low** | RTX 2060–3060, RTX 4060; AMD/Intel with RT | 1080p–1440p, DLSS Balanced/Performance | ≤ 1.5 ms |
| **Medium** | RTX 3070/3080, 4060 Ti/4070 | 1440p, DLSS Quality | ≤ 4 ms |
| **High** | RTX 4080/4090, 5080 | 4K-class, DLSS Quality (today's ini) | ≤ 5 ms static / ≤ 7.5 ms pan |
| **Ultra (5090)** | RTX 5090 at 3840x1600 120 Hz | §5.4 | §5.4 |

**How the other classes are extrapolated** (all of it, since nothing was measured off the
5090):
- Pixel-proportional passes scale with render pixels. 1440p Quality is 1707x960 = 0.60x the
  measured 2.73 MP; 1080p Balanced about 0.25x.
- The **scene build does not scale with resolution.** XFB, convert and refit scale with
  triangles, about 3M per frame.
- RT and tensor throughput relative to a 5090 (my rough factors from public benchmark
  ratios, not measured here):
  | GPU | Throughput vs 5090 |
  |---|---|
  | 4090 | ≈0.7x |
  | 4070 / 3080 | ≈0.35–0.4x |
  | 4060 / 3060 | ≈0.2x |

**Result:** on a 4060-class GPU the static scene build alone is ≈1.9 / 0.2 ≈ **9 ms**, and
RR at 1080p is ≈ 2.6 × 0.34 / 0.2 ≈ **4.4 ms** (vs ≈1.4 ms for SR). The effects are cheap
by comparison. **So Low turns off the hardware scene build and RR, not individual rays.**

### 5.2 Preset table (key values)

| Setting (key) | Low | Medium | High | Ultra (5090) |
|---|---|---|---|---|
| Game DLSS mode (graphicSettings) | Balanced/Perf (user's choice) | Quality | Quality | **Quality** (DLAA only if measured to fit, §5.4) |
| Ray Reconstruction `rr` | **0** (SR) | 1 | 1 | 1 |
| Hardware scene `rt_build` (derived) | **0** | 1 | 1 | 1 |
| Alpha-tested casters `rt_alpha` (restart) | 0 | **0** | 1 | 1 |
| Sun shadows `rt_shadows` | 0 → contact shadows `contact_shadows=1` | 1 | 1 | 1 |
| GI `gi` / `gi_rt` | 1 / 0 (screen-space, `gi_rays=2`, `gi_steps=12`) | 1 / 1, `gi_rr_rays=1` | 1 / 1, `gi_rr_rays=1` | 1 / 1, **`gi_rr_rays=2`** |
| Local-light shadows `rt_local_shadows` / `ls_samples` | 0 | 1 / **1 (RIS)** | 1 / 0 (every light) | 1 / 0 |
| Shadowed lights `ls_shadowed` | – | 0 | 0 | **2 (min)** |
| Reflections `rt_reflections` / `rf_max_rough` | 0 | 1 / **0.4** | 1 / 0.6 | 1 / **0.8** |
| Frame generation `fg` | 2 if RTX 40+, else 0 | 2 if RTX 40+ | 2 (3/4 on RTX 50 if the display needs it) | **2** |
| Reflex `reflex` | 1 | 1 | 1 | 1 |
| Frame cap (new, §6) | off | off | off | **58 rendered (116 displayed)** |
| **Added cost on its target class (extrapolated)** | ≈0.5–1.2 ms (SS GI 0.18 ms at 5090 1x scale → ×0.25 px ÷0.2 GPU ≈ 0.25 ms; contact shadows) | ≈3.5–4.5 ms (build ≈ (0.6+0.27+0.39)/0.37 ≈ 3.4 ms + effects at 0.6x px ≈ 0.6 ms + RR delta) | ≈5 ms static / ≈7 ms pan on a 4090 at 4K-class | **measured base ≈ 4.7 ms static / ≈ 7.2 ms pan**, + ≈ 0.4–0.6 ms (extrapolated) |

Notes:
- **Low without RT.** No ray tracing, but GI, contact shadows and the AO fold still run; this
  is what the code does with `gi_rt=0` and no TLAS. A GPU without ray query gets Low
  automatically.
- **Medium drops `rt_alpha`.** That halves the refit (0.88 → 0.39 ms on a 5090, about −1.3 ms
  on a 4070-class card). The cost is false shadows under grates and chains (1.9% of pixels,
  `TRACK_D_RT.md` 6.7). It needs a restart, so the launcher sets it before launch.
- **High is today's `bg3rtx.ini`.**

### 5.3 GPU capability rules (applied on top of any preset)

| Condition | Rule |
|---|---|
| No ray query / AS (pre-RTX NVIDIA, AMD < RDNA2, older Intel) | force Low; hide the RT knobs; `rt=0` at device creation (nothing to enable) |
| Not NVIDIA (AMD RDNA2+, Intel Arc) | `rr=0`, `fg=0`, no NGX. RT knobs available but **untested**: label "experimental". The game's upscaler is FSR/XeSS. Our RR hook only fires on DLSS evaluates |
| NVIDIA RTX 20/30 | RR yes (RR runs on all RTX; [NVIDIA](https://www.nvidia.com/en-gb/geforce/news/nvidia-rtx-games-engines-apps/), [DLSS 4.5 RR on all RTX](https://kami-labs.fr/en/tech/dlss-4-5-ray-reconstruction-toutes-rtx/)), FG no (DLSS-G needs RTX 40+) |
| RTX 40 | FG 2x only |
| RTX 50 | FG 2x/3x/4x (MFG is RTX 50 only, same NVIDIA source) |
| **Game upscaler is not DLSS** (FSR, XeSS, TAA, off) | RR cannot run. Everything must take the **non-RR path** (§7.3 "effective RR") |
| Streamline reports DLSS-G unsupported | `fg` forced to 0 and greyed out (`[sl] DLSS-G supported on this adapter` is already logged) |

### 5.4 Ultra (5090): the personal top preset

Hardware: RTX 5090, 3840x1600 HDR10 at 120 Hz. Proposed target: FG 2x locked at 120
displayed, rendered ≥ 65 fps, 1% lows ≥ 60.

**Refinement of the target:**
1. **The arithmetic.** At 120 displayed with 2x, the rendered rate is exactly 60. "≥ 65
   rendered" is the **uncapped capacity** we need, not the locked rate.
   - Use a frame cap of **58 rendered / 116 displayed** (a little under refresh, as with
     VRR, so FG never hits V-sync back-pressure).
   - Require the **uncapped** rate to stay ≥ 65 fps, with p1 ≥ 60, **during camera pans**.
   - The cap does not exist yet. Streamline's `sl::ReflexOptions::frameLimitUs` is the
     natural place; the layer already calls `slReflexSetOptions`, so the cap is a new
     `fps_cap` knob.
2. **Pans, not static views, set the budget.** Measured 103 rendered fps = 9.7 ms. Since that
   measurement, D-LS +0.35 and D-RF +0.33 were added, so ≈ **10.4 ms static**.
   - A pan changes the culled draw set, so the BLAS rebuilds instead of refitting:
     3.4 vs 0.88 ms, **+2.5 ms**.
   - `rt_rebuild_frames=60` also forces a rebuild every 60th frame. That alone lands in the
     1% lows.
   - Pan frame ≈ **12.9 ms**. Against the 15.4 ms (65 fps) line, **pan headroom is ≈ 2.5 ms,
     not 5–6.** Static headroom is ≈ 5 ms.
   - This needs one measurement, `auto-run.ps1 -RealPresent -Move` with all effects on,
     reading the p1 frame time.

**Ranked spend list** (quality per ms first). Costs are extrapolated from the measured
passes unless stated.

| # | Upgrade | Keys | Est. cost | Quality value | Verdict |
|---|---|---|---|---|---|
| 1 | Shadowed local lights: min of game atlas and traced | `ls_shadowed=2` | +0.05–0.2 ms (extra rays only where a shadowed light covers the pixel; unmeasured, no such scene captured) | high where it applies: the atlas is a low-res tetrahedral map. `=2` keeps the atlas's off-screen casters; `=1` would lose them (TLAS is camera-culled) | **take** |
| 2 | Reflections to rougher surfaces | `rf_max_rough` 0.6 → 0.8 | +0.1–0.2 ms (traced pixel share grows; base 0.33 ms) | medium: glossy metal/stone stop falling back to the probe. RR handles the noise. 1.0 adds little and costs more | **take** |
| 3 | 2 GI rays per pixel | `gi_rr_rays=2` | +0.15–0.2 ms (trace 0.175 ms per ray) | medium: less RR boil in dim interiors. GI reach is still 1 m | **take** |
| 4 | Remove the forced periodic rebuild | `rt_rebuild_frames` 60 → 600 (internal) | **saves** ≈ 0.04 ms average and removes one 2.5 ms spike per second | p1 improvement | **take** (internal; needs a refit-quality check over 10 s) |
| 5 | DLAA (game DLSS mode) with RR | game `DLSS2Mode` | **+5–7 ms** (2.25x render pixels: our per-pixel passes ≈ 1.0 → 2.3 ms; game raster, assuming ~70% pixel-bound, ≈ +3.5 ms; RR input 2.25x ≈ +1–2 ms) | high on paper, but RR at Quality already reconstructs a 4K-class output well | **not in the default Ultra**: static ≈ 16–17 ms (≈ 60 fps) fails the ≥ 65 capacity rule, and pans fall below 58. Offer it as a choice and measure; revisit after the static/dynamic BLAS split (ROADMAP 6.7 item 3, recovers ≈ 2.5 ms in pans) |
| 6 | Multi-bounce GI (D-GI2, screen-seeded SHaRC) | not implemented | 0.4–0.6 ms, 160 MiB (`TRACK_D_RT.md` 8) | high (room-scale reach, colour bleeding) | **reserve the budget**; becomes Ultra's first add when it exists |
| 7 | Longer rays | `gi_rt_radius`, `rf_tmax`, `rt_tmax` | ~0 to +0.1 ms (traversal is cheap, ≈ 0.04–0.07 ms per M short rays) | **negative today**: the TLAS lacks off-screen and culled geometry, and `gi_rt_fallback=0` treats a hidden/off-screen hit as occlusion only. Longer GI rays mostly darken. `rf_tmax=100` and `rt_tmax=300` are already scene-scale | **skip** until D-GI2 and cascade capture (D-EN3) |
| 8 | D-EM data: capture, emissive BLAS/TLAS, light list (`TRACK_D_RT.md` 13) | `rt_emissive=1` | **0.7 ms** (measured on the 5090: effects-pass capture +0.044 ms; build and light list 0.39-0.62 ms median per scene, WQ 0.39-0.44, AF 0.43-0.50, GF 0.62 at 519k triangles; 0.044 + 0.62 rounded up = 0.7) | high: the only source of the game's emitters for ray-traced lighting (lava, glow maps, decals) | **take**, from the pan headroom (approved 2026-10-27). The 0.7 ms is an optimisation target, not a gate (user directive: on Ultra a budget never drops or degrades a physically better result). A BLAS refit while the draw list is unchanged is to lower it |

**Ultra (5090) = High + #1–#4 + `fps_cap=58`.** Estimated static ≈ 10.9 ms (≈ 92 fps
uncapped) and pan ≈ 13.4 ms (≈ 75 fps). Both clear 65 with margin, so the 58 cap holds and
p1 stays at the cap. Leftover headroom (≈ 2 ms in pans) is kept for D-GI2. FG 3x/4x is
pointless at 120 Hz, as proposed.

**Pan headroom ledger (15.4 ms line, 65 fps).** It starts from the Ultra pan of ≈ 13.4 ms,
which includes neither SHaRC nor D-EM:

| Item | Claim | Headroom left |
|---|---|---|
| Ultra pan | 13.4 ms | 2.0 ms |
| SHaRC / D-GI2 (#6) | up to 0.6 ms | 1.4 ms |
| D-EM data (#8) | 0.7 ms | **0.7 ms** |

The static frame is then ≈ 10.9 + 0.6 + 0.7 = 12.2 ms, leaving 3.2 ms. A new claim has to
fit the 0.7 ms left in pans, or lower an existing one first (the D-EM refit). On Ultra the
ledger guides optimisation only: it is never a reason to drop or degrade a physically better
result (user directive, 2026-10-27).

---

## 6. Exposed knobs (10)

**Rule for exposing a knob.** A key is exposed only if all four hold:
1. **A player can see the difference** without a debug view.
2. **It has a monotonic cost/quality trade**, or it is a clear on/off.
3. **No value in the exposed range breaks correctness.** It cannot over-brighten, cause leaks,
   produce wrong composition, or invalidate validation.
4. **It is not hardware plumbing.**

Everything else stays ini-only (class A tinkerers) or internal (classes N, D). Composite
knobs map one label to several keys, so users never see `gi_rr_rays` vs `gi_rays`.

| # | Label | Values | Keys it drives | Low | Med | High | Ultra | Apply |
|---|---|---|---|---|---|---|---|---|
| 1 | **Preset** | Low / Medium / High / Ultra / Custom | everything below | – | – | – | – | per knob |
| 2 | **Ray Reconstruction** | Off / On (greyed if not NVIDIA RTX or game upscaler ≠ DLSS) | `rr` | Off | On | On | On | live, one history reset |
| 3 | **Ray-traced geometry** | Off / Opaque only / Full (with alpha-tested) | `rt_build` (derived), `rt_alpha` | Off | Opaque | Full | Full | Off/Opaque live; Full ↔ Opaque **restart** |
| 4 | **Sun shadows** | Screen-space / Ray-traced | `rt_shadows`, `contact_shadows` | SS | RT | RT | RT | live |
| 5 | **Global illumination** | Off / Screen-space / Ray-traced / Ray-traced High | `gi`, `gi_rt`, `gi_rays`/`gi_steps` (SS), `gi_rr_rays` 1/2 | SS | RT | RT | RT High | live (RR on/off reallocates) |
| 6 | **Local light shadows** | Off / Fast (RIS) / Full / Full + shadowed lights | `rt_local_shadows`, `ls_samples` 1/0, `ls_shadowed` 0/2 | Off | Fast | Full | Full+ | live |
| 7 | **Reflections** | Off / Glossy (0.4) / Standard (0.6) / Rough (0.8) | `rt_reflections`, `rf_max_rough` (RR off: mirror only, `rf_max_rough_norr` 0.3) | Off | Glossy | Std | Rough | live |
| 8 | **Frame generation** | Off / 2x / 3x / 4x (by GPU) | `fg` | 2x on RTX 40+ else Off | same | 2x | 2x | Off ↔ on: **restart**; 2/3/4 live |
| 9 | **RT lighting intensity** | 50–100%, step 5 | `gi_strength`, `ls_strength`, `rf_strength` (scaled together) | 100 | 100 | 100 | 100 | live |
| 10 | **Frame cap (rendered fps)** | Off / 30–240 | new `fps_cap` → Reflex `frameLimitUs` | Off | Off | Off | 58 | live |

Plus **Reflex** (Off / On / Boost, `reflex`, default On) under an "Advanced" fold, because
DLSS-G needs it on. The **game's DLSS mode** stays in the game's own Graphics menu, which
already handles the render-target recreation. The overlay shows it read-only and warns when
it is not DLSS (§5.3). The launcher may set it before launch.

Changing any knob switches the preset combo to **Custom**. "Reset to preset" restores.

---

## 7. Recommendation and plan

### 7.1 Approach

- **Primary: our own overlay** (§3) plus the **launcher/config exe** (§4), sharing one knob
  table.
  - **No user dependency.** Our FG and present path need no SE, MCM or pak mod, and
    distribution needs a launcher anyway.
  - **Exact order against DLSS-G.** We control it: FG is paused while the menu is open.
  - **Live preview.** You see the image and the timing change as you drag.
  - **Stability.** Nothing outside our code hooks the swapchain.
- **Fallback: the launcher alone.** If the overlay's input capture proves unreliable under
  SDL2, the launcher is a complete settings UI. It works while the game runs, because the
  ini hot-reloads.
- **Optional: an MCM bridge** for SE users (Insert / ESC-menu entry), writing the same
  `user.ini`.

### 7.2 Phases and effort (one developer familiar with the code)

| Phase | Work | Effort |
|---|---|---|
| **1. Settings model** | §7.3 code changes; preset table + knob table header (`common/settings.h`); `bg3rtx_user.ini` layering; release defaults for D keys (`dump_shaders=0`, `dev=0` gates the tracer hotkey); capability detection; `fps_cap` | 2–3 days |
| **2. Launcher exe** | Win32 + ImGui DX11; GPU detect; preset suggestion; `graphicSettings.lsx` DLSS mode; launch with env; "Steam launch option" docs | 2–3 days |
| **3. Overlay** | ImGui Vulkan in the present hook (own semaphore), PQ/sRGB pipeline, WndProc subclass + raw-input swallow, hotkey, FG pause/restore, live timings, disabled when headless; validation run with the menu open and `fg=2` | 5–7 days |
| **4. Preset validation** | `auto-run.ps1 -RealPresent -Move` per preset on the 5090 (p1 frame time, pan), plus at least one 30-series and one 40-series card (or 5090 power-limited/downclocked as a proxy, clearly labelled); tune the table | 2–3 days + access to GPUs |
| 5. (optional) MCM bridge | `MCM_blueprint.json`, 20-line Lua mirror to `user.ini`, `cfg_poll` second path | 1–2 days |

**Total: ≈ 2.5–3.5 weeks** for phases 1–4.

### 7.3 Code changes the settings layer needs (phase 1)

1. **Effective RR.** `gi_res_shift()`, `ls_soft` and `rf_rr` (`core/gi.cpp`) decide "RR is
   denoising" from `cfg_int("rr")`.
   - **The bug.** With the game on FSR/TAA, or RR creation failed (`rr_failed`), they still
     run 1-spp GI, stochastic soft shadows and GGX reflections **with no denoiser**: visible
     noise.
   - **The fix.** The host exposes "RR evaluated in the last 2 frames" (`ngx.cpp` already
     knows `last_mode`). The core uses that instead.
   - It is the prerequisite for AMD/Intel and for "game upscaler ≠ DLSS".
   - **Done (2026-10-26).** Host API ABI 7 adds `rr_active()`: true while an
     RR evaluate succeeded in the last 3 frames (`ngx_rr_active`,
     `layer/ngx.cpp`). `gi_rr`, `ls_soft` and `rf_rr` at -1 follow it
     (`core/gi.cpp`). With FSR/TAA no evaluate happens, with an SR fallback or a
     failed creation (`rr_test_fail=1` simulates one) it stays false, so the
     effects use their non-RR modes. On a level load RR pauses for a few frames
     and the GI drops to its half-resolution mode until RR runs again (logged
     as `gi: resources for ...`).
2. **Derived `rt_build`.** Build the scene only if any RT consumer is on (`rt_shadows`,
   `gi_rt`, `rt_ao`, `rt_local_shadows`, `rt_reflections`). Today `rt_build=1` with every
   effect off still spends ≈ 1.9 ms static / 4.4 ms pan.
3. **Layered config.** `cfg_raw` order: user ini → preset table (compiled) → `bg3rtx.ini`
   (dev/shipped defaults) → code fallback. Write the user ini atomically. `cfg_poll` already
   tolerates the empty mid-write file, not a partial one.
4. **`fps_cap`** through `sl::ReflexOptions::frameLimitUs` (with FG on), and a simple sleep
   limiter otherwise.
5. **Release defaults.** D keys off; `rt=1` only if the device reports ray query (it already
   logs and turns itself off when features are missing).

### 7.4 How changes apply live

| Category | Keys | What happens |
|---|---|---|
| **Live, next frame** | all strengths, `rt_shadows`, `rt_local_shadows`, `ls_samples`, `ls_shadowed`, `rt_reflections`, `rf_max_rough`, `gi`, `gi_rt`, `gi_rays`, `gi_rr_rays`, `gi_steps`, `reflex`, `fps_cap`, `fg` 2↔3↔4↔0 (with Streamline loaded) | push constants / per-frame branches; the next present's `cfg_poll` picks them up (≤ 1 s) |
| **Live with a reset or reallocation (brief hitch, one frame of history lost)** | `rr` (RR ↔ SR history reset, GI images reallocated full ↔ half res via `gi_res_shift`); `rt_build` 0→1 (first build is a full rebuild, ≈ 3.4 ms once) | handled by existing code; badge "brief hitch" |
| **BVH rebuild** | `rt_alpha_test`, `rt_refit`, `rt_rebuild_frames` | the next build is a rebuild; live but internal |
| **Game-side** | DLSS mode / upscaler | game's own menu; it recreates SR, and our RR follows that feature's lifetime |
| **Restart** | `rt` (device features), `rt_alpha` (capture source + memory), `fg` 0 → 2+ (Streamline loads at instance creation), memory budgets, `ngx_hook`, `sl_*`, `force_*`, `geo_capture` | the overlay shows "applies after restart" and writes the value; the launcher sets them before launch |

So, among the user-facing knobs, only **"Ray-traced geometry: Full ↔ Opaque"** and
**"Frame generation Off ↔ On"** need a restart. Everything else applies within a second.

---

## 8. Main risks

1. **SDL2 input capture** (overlay). Raw-input or polled paths may bypass our WndProc
   subclass, so the mouse would move the camera behind the menu. Prototype this first. The
   fallback is the launcher.
2. **Presets below the 5090 are extrapolations.** No other GPU class has been measured. The
   scene build (not resolution-scaled) is the uncertain big term. Phase 4 must measure at
   least one 30/40-series card before the presets ship.
3. **Effective-RR bug (§7.3.1).** Without it, the first FSR or AMD user sees noisy GI,
   shadows and reflections. This is the one correctness fix that blocks distribution.
4. **Non-NVIDIA RT is untested.** Ray query on AMD/Intel has run neither the alpha-test
   any-hit path nor the XFB capture. Ship it as "experimental" or Low-only.
5. **Third-party overlays and hooks** (SE IMGUI, ReShade, RTSS, Steam overlay) drawing into
   the back buffer behind DLSS-G are not in the UI tag. That is outside our control. The
   launcher already disables the Steam/EOS/GOG Vulkan overlay layers for the dev setup;
   whether to do that for users is a policy choice.
6. **Ultra (5090) pan budget.** It rests on the ≈ 2.5 ms rebuild cost per pan frame. DLAA
   becomes viable only after the static/dynamic BLAS split. Measure the p1 frame time with
   `-RealPresent -Move` before fixing the preset.
7. **Distribution beyond settings.**
   - Runtime DXC shader compilation from `shaders/` means shipping HLSL and the DXC DLLs, or
     precompiling the SPIR-V.
   - Streamline binaries ship under `bin/sl`.
   - Both affect the launcher's install layout.

---

## 9. Implementation status (phase 1, 2026-10-26)

### 9.1 Done

| Item | Where | Notes |
|---|---|---|
| Settings table (single source of truth) | `common/settings.h` | Header-only, standard C++. It holds the entries of §6 (9 knobs + Reflex under Advanced; the preset is the combo), with labels, descriptions, choices → keys, per-preset values, apply class (live / hitch / partial restart), hardware caps per choice and dependencies between knobs. It also has the preset extras (Ultra: `rt_rebuild_frames=600`), `render_user_ini`, `suggest_preset` and `gpu_caps`. The phase-2 overlay can include it as is. `data/settings_schema.json` is the same table as JSON (also `BG3RTX.exe --dump-schema`); `settings_test` fails when the file is stale, and `settings_test.exe --write-schema` regenerates it |
| Layered config | `layer/util.cpp` `cfg_poll` | Order: `bg3rtx.ini` → `bg3rtx_user.ini` (`%BG3RTX_USER_INI%` replaces the path; `none` skips it) → `%BG3RTX_INI%`. **Change from §7.3.3:** the preset is not a separate layer. Any file may contain `preset=`, which expands at that file's position before the file's own keys, so a later file (test overrides) beats a user preset. Each layer is hot-reloaded on its own mtime, and a truncated layer keeps its previous keys. The log prints `[cfg] settings read`, `[cfg] knobs:` (choices + matching preset) and `[cfg] keys:` |
| Derived `rt_build` (§7.3.2, partly) | `settings::resolve_derived` | Presets write `rt_build=auto`, which resolves to 1 only while an RT consumer (`rt_shadows`, `rt_ao`, `gi_rt`, `rt_local_shadows`, `rt_reflections`, `rt_debug`) is on. `bg3rtx.ini` keeps `rt_build=1`, so the developer default is unchanged. `rtscene.cpp` is untouched |
| `fps_cap` (§7.3.4) | `layer/streamline.cpp` `reflex_apply`, `layer/framecap.cpp` | With Streamline active, `slReflexSetOptions` gets `frameLimitUs = 1e6/fps_cap`, and `reflex`/`fps_cap` are re-applied whenever they change (Reflex used to be set once). Without Streamline, a sleep limiter (high-resolution waitable timer, then a spin) runs in `QueuePresentKHR` |
| Release defaults (§7.3.5) | `data/release.ini`, `scripts/package.ps1`, `layer/tracer.cpp` | The trace hotkey needs `dev=1` (code default 0; the repository's `bg3rtx.ini` has `dev=1`). The `dump_shaders` code default is now 0; the repository ini keeps 1. `package.ps1` builds `dist/BG3RTX` with `bg3rtx.ini` = repository ini + `release.ini` (D keys off) |
| Launcher | `tools/launcher` → `bin/BG3RTX.exe` (target `bg3rtx_launcher`, `build.ps1 -Launcher`) | The window is generated from the table: preset combo + "Use suggested", knobs, markers (live / brief hitch / restart required / unmet dependency / not supported), description panel, game folder (Steam registry + `libraryfolders.vdf`, or Browse), Save (atomic: temp + `MoveFileEx`), Save and launch. There is also a command line (`--launch`, `--dry-run`, `--preset`, `--detect`, `--dump-schema`) and a Steam `%command%` mode |
| GPU detection (§5.3) | `tools/launcher/gpu.cpp`, `settings::gpu_caps` | A Vulkan instance in the launcher process (implicit layers disabled) reads the ray-query and AS extensions. The RTX series comes from the name, or from the PCI device ID for workstation names. Unsupported choices are marked and presets are clamped. Without ray query the launcher writes `rt=0` |
| Tests | `tests/settings_test.cpp` (`build.ps1 -Test`, `ctest`) | Parsing, preset expansion, layer order, `rt_build=auto`, read-back of every preset, "the developer ini equals High", the release profile, the user-ini writer (keeps hand-edited keys), dependencies, GPU rules, the schema file |
| Test harness | `scripts/auto-run.ps1` | `auto.ini` now holds only the overrides (the layer reads `bg3rtx.ini` itself), so `-Set preset=<id>` works. `-UserIni <file>` layers a launcher file. By default `BG3RTX_USER_INI=none`, so test runs ignore the player's file. `live_phases.py` still works |

**Launcher toolkit: plain Win32 + common controls, not Dear ImGui (change from §4).**
- The window is about 15 standard controls generated from the table. Native controls bring
  keyboard navigation, DPI scaling and screen-reader support at no cost.
- It needs no third-party source in the repository (Dear ImGui is not vendored, and
  `external/` is not versioned) and no DX11 device.
- The overlay needs ImGui with the Vulkan backend anyway. It shares the table rather than the
  drawing code, so the two UIs still cannot drift.

### 9.2 Verified

- `settings_test`: 120 checks pass.
- `BG3RTX.exe --detect` reports the RTX 5090 (0x2B85): ray query yes, series 50, suggested
  preset Ultra.
- **Launch path.**
  - `--launch --dry-run` gives `bin\bg3.exe` in the game folder, with `VK_ADD_LAYER_PATH`,
    `VK_INSTANCE_LAYERS`, `VK_LOADER_LAYERS_ENABLE`, `SteamAppId`/`SteamGameId` and
    `DISABLE_LAYER_NV_PRESENT_1` (fg ≥ 2).
  - The Steam `%command%` form (`Launcher\LariLauncher.exe`) resolves to `bin\bg3.exe`.
  - A real `CreateProcess` with `--exe cmd.exe` shows the layer variables in the child and an
    inherited `BG3RTX_INI` removed.
  - The game itself was not started through the launcher.
- **Window layout:** checked off screen with `--screenshot`.
- **Game run:** `auto-run.ps1 -Background -UserIni <launcher-written preset=ultra>`, headless.
  The trace completed. `bg3rtx.log` shows:
  - `settings read: bg3rtx.ini (83 keys) + bg3rtx_user.ini (1 keys, preset=ultra) + auto.ini (4 keys)`
  - `keys: … gi_rr_rays=2 … ls_shadowed=2 … rf_max_rough=0.8 fg=0 … fps_cap=58 … rt_rebuild_frames=600 (rt_build from auto)`.
    `fg=0` is the headless override winning over the preset, as intended.
  - `[framecap] fps_cap 58: sleep limiter in the present path`
  - `[watch]` ≈ 290 presents per 5 s, which is the 58 fps cap.

### 9.3 Not done / unverified

- **Reflex frame limit with DLSS-G** (`frameLimitUs`): compiled but not run, because that
  needs `-RealPresent` with `fg=2`. Whether the Reflex limiter applies with Reflex mode off is
  also untested.
- **Effective RR (§7.3.1):** not done. It needs `ngx.cpp` / `core/gi.cpp`, which other work
  owns. The launcher greys out RR only on non-RTX GPUs; it cannot see the game's upscaler.
- **DLSS mode in `graphicSettings.lsx`** (§4 item 4): the launcher does not write it.
- **Ultra's `rt_rebuild_frames=600`:** refit quality over long pans is unchecked (§5.4 #4).
- **Presets below the 5090:** still extrapolations (§8 risk 2).
- **The release package (`dist/`):** assembled, but not run from a clean folder (DXC and the
  shader layout untested).
- **Not covered by tests:** Steam library detection, and the UI's interactive paths (combo
  changes, Browse).

## 10. Defaults: one source (2026-10-27)

`common/settings.h` holds every default:
- `kDefaults` is every key `bg3rtx.ini` sets, with its value, in file order.
- `kReleaseDefaults` holds the keys `data/release.ini` switches off.
- Both tables are generated by `python scripts/gen_settings_defaults.py` (`--check` only
  compares). Run it after editing either ini.
- `tests/settings_test.cpp` (`test_defaults_one_source`) asserts that the files and the tables
  agree exactly: the same keys and the same values.

**The fallback.** `code_default(key)` is `kDefaults` with the release profile on top, so a
missing developer key is off (`dump_shaders`, `dev`).
- The layer (`layer/util.cpp`) merges it as layer 0, under `bg3rtx.ini`, `bg3rtx_user.ini`
  and `%BG3RTX_INI%`. A key no file sets therefore has its table value, and `rt_build=auto`
  still resolves.
- `cfg_int`/`cfg_float` also return it before the first `cfg_poll`.
- A call site's literal is used only for keys in neither table.
- When a literal disagrees with the table, the layer logs it once per key and literal:
  `[cfg] code default for X (lit) differs from settings.h (val)`. Non-numeric defaults
  (`rt_build=auto`) are not compared.
- `kCodeDefaults` is gone: the knob read-back (`settings::lookup`) uses `code_default` too.

**High = the developer defaults.** The test checks two things:
- every key the High preset writes has the table's value;
- the table alone reads back as High.

It also checks that a `preset=low` layer over the defaults still expands and reads back as
Low.

**Literals fixed on 2026-10-27** (each was dead once the table existed; all now equal
`bg3rtx.ini`):
- `core/gi.cpp`: `gi_rays` 2→4, `gi_steps` 12→16, and `gi`, `gi_rt`, `rt_shadows`,
  `rt_local_shadows`, `rt_reflections` 0→1;
- `core/sun_trust.cpp`: `rt_sun_top` 1→0;
- `layer/{rtscene,geocapture,devfault,alphatest}.cpp`: `rt` 0→1, `rt_alpha` 0→1;
- `layer/{layer,streamline}.cpp`: `fg` 0→2;
- `layer/ngx.cpp`: `rr_preset` 0→6.

A key read in code but absent from `bg3rtx.ini` (`assetdb_join`, `dev_fault`, ...) keeps its
literal. The 2026-10-27 scan matched one-line `cfg_int`/`cfg_float` calls with a numeric
literal in `core/` and `layer/`. Calls split over lines and computed fallbacks were not
scanned; the runtime log catches them.
