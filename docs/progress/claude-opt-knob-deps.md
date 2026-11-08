## Launcher: a selected option turns on what it needs (branch claude/opt-knob-deps, done offline, 2026-11-08)
- `settings::raise_needs` (common/settings.h), called by the web bridge's "set": choosing a choice raises every
  knob it needs to the needed choice, recursively, all or nothing. Hidden, custom, experimental-without-opt-in or
  unsupported needs raise nothing (today's behaviour, the card keeps the reason). Status "Also turned on: ...".
  Turning a prerequisite off still lowers dependents at Save (enforce_needs, unchanged).
- RTX Hair: "Strand hair on screen" On from all-off (Ray-tracing detail full) now also sets Strand hair in RT
  lighting and Hair shadows; the saved file holds the whole strand set plus rt_native_hair/scalp.
- web_bridge_test: the new hair case, an unmeetable need (no LSS), lowering on prerequisite off; the FG -> Reflex
  and Play-review cases now expect the raise at selection. No presets, keys or shaders changed.
- User check: launcher with dev=1 and experimental shown, set only "Strand hair on screen" On, Save: Hair shadows
  and Strand hair in ray-traced lighting show On, the restart note appears.
