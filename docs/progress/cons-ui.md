## Release UI/UX review: launcher, settings texts, installer, player docs (branch cons-ui, tests PASS, 2026-11-05)

- 26 changes, offline only. No game launch. CTest 167/167 pass (settings, schema, bridge, launcher UI).
- Experimental shows only Water surface reflections in this release (`kOptInKnobs`); strand hair returns
  with its post-release update. Before: 8 unusable "unqualified" hair cards; one could be turned on.
- Screenshot mode now renders the real settings table (Ultra, reference RTX 5090, no player files read)
  instead of a stale JavaScript sample. `--screenshot-scale` and `--screenshot-bottom` now work in the WebView.
  New views: surfaces, performance, needs, preset.
- Needs a layer rebuild because settings.h changed: rerun verify_rc before the release.

| Page | Before | After | Why |
|---|---|---|---|
| Experimental | 8 strand hair cards and Fresnel, "unqualified" wording | Water surface reflections only | Release facts; no empty promises |
| Experimental | "Water interface Fresnel", "On (unqualified)" | "Water surface reflections", On; plain help, restart stated | Plain wording |
| All cards | Badge showed only the current choice ("Applies after saving" on water Off) | "May need a restart" when another choice needs one; tooltip lists them | Restart state was hidden |
| Lighting | Awkward help for Ray-tracing detail, Sun shadows, Indirect lighting | Plain one-line help; two rays named | Clarity |
| Lighting | Off-screen bounce and hair shadows had no cost | Cost lines from recorded RTX 5090 numbers | Report cost of heavy effects |
| Lighting/Surfaces | Details said "Requires..."; DoF said "paired tests did not establish an improvement" | "Needs ...", restart rules, where to look | Consistent, truthful |
| Surfaces | Water had no cost or restart detail | Estimate 0.5-0.7 ms, 520 MB, restart rules | Cost and restart |
| Performance | Order FG, limit, Reflex; vague FG details | FG, Reflex, limit; on/off restart, 2x/3x/4x live | Dependency order, restart truth |
| Header | "GPU not detected · Not detected" | No duplicate | Duplicate text |
| Header | Preset tooltip: suggestion only | Preset description plus suggestion | Explain presets |
| Preset review | "58 -> 0", no restart marks, no summary | "58 fps → Off", "(after a game restart)", preset description | Readable, restart state |
| Help & support | Video memory "32 GiB" twice | Once | Duplicate |
| Help & support | No "tested only on RTX 5090" | Compatibility says it | Release fact |
| Help & support | Several GPUs: "Graphics check failed" | "Several GPUs found. Choose the one the game uses" | Wrong message |
| Help & support | Comparison/overlay text | Ctrl+Shift+F9 explained; overlay advice for start/FG failures | Shortcut explained |
| First run | Missing game folder only in the footer | Notice on every page with "Choose game folder…" | Play was disabled without a clear reason |
| Native fallback | "(awaiting qualification)", "(experimental)" | "(Experimental)"; same help texts | No internal wording |
| CLI | "recheck in Overview" (no such page) | "recheck in Help & support" | Stale |
| Installer | One-line welcome; claims nothing about hardware | Welcome: what it installs, Vulkan + DLSS, tested only on RTX 5090 | Clear steps |
| Installer | Default finish page; errors pointed to SETUP.md (not shipped) | Finish text names the next step; errors say run setup again or report | Clear steps, useful errors |
| USER_SETTINGS.md | Rows out of page order; "Disable other Vulkan overlays in Help & support" (removed); F10 claim | Page order, Experimental row, Ctrl+Shift+F10 truth (no eligible option yet) | Docs match UI |
| Release README | "Browse...", removed overlay checkbox, "Ultra" OMM only, jargon cost table, stale smoke line | Current names, High+Ultra OMM, plain cost table, RTX 5090 note, Ctrl+Shift+F10 truth | Docs match release |

Screenshot pairs (`.scratch/ui-review/before|after/`, untracked; 100/125/150/200%, and 720-wide):
lighting, lighting-bottom, surfaces, performance, support, support-bottom, experimental (all four scales);
needs, preset, report, missing, probe, error, restart (100/200%); advanced-1; lighting/experimental/performance-1-736.
Layout: identical at all scales (the window keeps its logical size), no clipped text or wrapped buttons found.
