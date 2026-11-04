## Release raw migration and qualified exact-k (branch release-fix2, kept, 2026-11-04)

- Normalize raw `ls_shadowed=1` even when no complete choice matches: Fast -> 0,
  Full -> 3; preserve `ls_samples` and whether local shadows are enabled. Developer files stay intact.
- Audited all visible knobs with hidden raw choices: `local_shadows`, `box_lights`,
  `exp_hidden_surface`, `exp_native_coverage`. The other three already normalize raw keys.
  Tests cover partial box mode 1, Tilesets, reflection shade/GI and emission mode 2 across all presets.
- User decision: qualified exact-k k1 is automatic in Ultra with Full local shadows and RT geometry,
  with no release control; stale k/rest overrides are normalized. Lower presets remain Off:
  the approved Full-path evidence does not qualify them. Developer overrides remain available.
- Approved S7 inn: local shadows 0.717 -> 0.555 ms (-23%); pre-post scene energy
  -0.38/-0.34/+0.12%, within OFF spread 0.89%. Contract, TRACK and measured metadata updated.
- Schema regenerated; defaults check passed. Full MSVC build (vswhere/vcvars) passed.
  Settings: 4,672 checks; bridge: 987; pipeline async: 385. All four requested CTests passed,
  including launcher_web_bridge_json. Default shader identity: 251/251 identical to HEAD by input keys.
  Existing Ultra LS_K requests selected; variants catalogue delta 0; ABI unchanged.
- Check in game when convenient: S7 inn, Ultra, existing `ls exact-k: active` and `ls:` GPU lines.
  No game launched, queue used, Git metadata changed or commit made for this task.
