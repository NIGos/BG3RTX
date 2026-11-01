## Hit-material scene-wait fixes on by default; prepass hooks off the provider mutex (branch hit-material-defaults, 2026-11-01)

- sw3 22:13 VALID: lean_hooks 1 windows 0 scene-wait timeouts in every pan and rotation (lean 0: 1-7 per 120 frames),
  hit-material hooks ~2 ms instead of 5-6. Kept: rt_hit_material_recycle, rt_hit_material_capture_unlocked and
  rt_hit_material_lean_hooks default 1 (bg3rtx.ini, settings defaults, layer fallbacks); automatic, no GUI entry
  (kAutomaticKeys), no longer written by the Hidden-surface shading levels; keys kept for A/B (0).
- Remaining mutex waits (0.6-0.7 ms/frame): with lean_hooks the per-draw prepass hooks take a leaf `pending_mutex`
  instead of the provider mutex; the map's other writers hold both. Not measured live yet (expect ~0 waits in the
  `[cpu] hit material` "mutex waits" field).
- settings_test 910 checks, 93/93 CTests pass.
