## Water compose CPU refusal diagnostics (branch water-compose-reason, OFFLINE_VERIFIED, 2026-11-04)

- Append diagnostics to the existing compose stats line; preserve its previous wording and queue regexes.
- Report actual dispatched hit-shade specialization 0/1/2 pass counts, including cached or pending replacements,
  and passes whose effective flags contain 64. No gate, shader, setting, ABI or default behavior changes.
- Count requested-but-refused light-binding, input-lifetime, scene/material and frame/shift failures.
  Reasons can overlap; first mismatching light-list index is 0..6 (shader bindings 22..28), or -1 if none compared.
- Core build passed. CPU water/core and Python queue/water/live gate CTests: 11/11 passed.
  Queue regression covers the appended fields while retaining existing compose gate matches.
- Shader request delta: 0. No game launch or Git metadata writes; no commit created.
- GPU cost and the wos1/wo1 refusal cause remain unmeasured. On a separately authorized S8 run with
  rt_water_hit_shade=2, inspect the appended CPU fields alongside compose hit attempts; this is diagnostic evidence,
  not a quality qualification or a relaxed lifetime guard.
