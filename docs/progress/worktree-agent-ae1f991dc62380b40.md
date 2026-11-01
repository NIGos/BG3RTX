## GUI rationalization: 28 experimental entries -> 8 levels (branch worktree-agent-ae1f991dc62380b40, offline, 2026-11-01)

- What changed (docs/GUI_AUDIT_20261101.md, user-approved): `common/settings.h` kKnobs. Main list gains Roof sun
  shadows (`roof_shadows`) and Box light shadows (`box_lights`: Off / Shadowed / All, off in every preset).
  Experimental: Hidden-surface shading (Off / Materials / + Bounce / + Reflections, writes the 8 hit keys incl.
  `_recycle` and `_wind_rgb`), Emission at ray hits (writes `rt_emissive` itself), Far-field 6 m, RT water (Off /
  Reflections / + Shadows / + Refraction, 7 keys), Native coverage (Off / Hair + scalp / + Tilesets), Physical hair
  origins (advanced), OMM, cutout spill. Developer only (dev=1): lighting reference, exact-k, current cascade sun,
  `rt_hit_emission=2`. `fg_letterbox`: no entry, ini key kept (the layer has no automatic mode yet).
- Every ini key still works. Old files: a level is picked from its marker keys (`Choice::mark`), old knob ids
  resolve through `kKnobAliases`. The user's 11-01 file loads as Box All, Hidden + Reflections, Emission Hidden,
  RT water + Refraction; saving adds `_recycle`, `_wind_rgb`, `rf_hit_gi`, `rt_water_game_shadow` (2 need a restart).
- Restart rules fixed: `rt_hit_material_wind_rgb` (startup only), `rt_hit_material_instanced` 0->1. The launcher's
  restart marker compares against the saved files, so migrated keys show it. Save lowers choices whose dependency
  is off (`enforce_needs`).
- settings_test: level key sets, no orphan key, aliases, dependencies, restart notes, fixture migration
  (`tests/fixtures/settings/user_ini_20261101.ini`, copy of the user's file).

## How to test in game (replaces the per-key lists)

Settings window, Experimental (off in every preset), preset Ultra. "Restart" = raise the level, quit, start again;
lowering is live. Ctrl+Shift+F9 switches every RT effect off and on.
- **RT water** (S8 Water Queens House pool): Reflections (**restart**): columns and temple reflected. + Shadows
  (**restart**): far half of the pool in shadow, no specks; camp pond (S3) tree shadows. + Refraction (**restart**):
  the pool bed shifts with the view.
- **Hidden-surface shading**: Materials (**restart**): no visible change, Q/E rotation keeps shadows on their casters
  (buffer reuse). + Bounce (live): S3, never darker. + Reflections (live): S8 polished stone, rf_debug=8 L_a/L_b.
- **Emission at ray hits** (live, needs Hidden-surface shading): S6 lava in reflections.
- **Box light shadows** All: S6 forge, S7 inn, no light into the next room.
- **FG letterbox**: add `fg_letterbox=1` to bg3rtx_user.ini by hand (fake fullscreen on a larger desktop).
