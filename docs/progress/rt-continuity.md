## RT continuity and water tilesets (branch rt-continuity, READY_FOR_LIVE, 2026-11-03)
- Trial log: GI/sun/water waits at 15:02:12; local match/box waits at 15:02:13.606; tileset skip at 15:02:24.006.
- Log selected + Tilesets at 15:02:23.981. Hair + scalp alone writes tilesets off; clarified the description.
- GI, sun, local shadows and water retain the previous compatible pipeline while replacements build or fail.
- GI retains complete specialization keys; water prepares both hit layouts and falls back to ordinary water while cold.
- Water supplies native tileset constant 1008 instead of skipping. ABI/settings defaults/presets unchanged; shader requests +0.
- Visible live Experimental choices prewarm below normal priority once real layouts exist; demand outranks queued prewarming.
- Startup count: 1 RR capture + 12 RR post pipelines; GI/water extras and cumulative count are logged per entry/layout.
- Dev off extras: GI 1 per entry; water 1 hair, plus 1 compose-shift per compose entry and 2 hit modes per hit entry.
- Dev on also warms live hair specializations, 4 exact-k and 2 reference entries. Choices warm relative to startup; new combinations use continuity.
- Shader cache avoids repeat DXC; driver pipeline creation can still cost time. No game launch or git metadata writes.
- Checks: build + 132/132 CTests passed; pipeline selection 342 checks, 25 repeat passes. Identity: 236/236 unchanged, zero compiles needed.
- Water native gate: 19/19 requests, 152/152 comparisons; combined hit/shift: 9/9 requests, 96/96 comparisons. No shader source changed.
- Live continuity/first-toggle latency and GPU cost are unmeasured. Check S8 pool/native coverage after a separately authorized launch.

## Trial follow-up (rt-continuity, READY_FOR_LIVE, 2026-11-03)
- User trial 15:07-15:10: reflection 73.7M hit attempts (11.1M hidden, 14.3M offscreen), compose 0.
- Host accepts F-1 only by borrowing an existing exact slot/generation query; current-frame camera checks remain.
- Borrowing recordings pin query storage and slot resources until every query user retires; CPU lifetime tests added.
- Water selects its separate ordinary/hit pipeline cache before layout validation; ordinary layouts ignore inactive hit sets.
- Native tileset INI edits wait for restart in the layer; launcher reports the selected choice apply mode. Hair/scalp stay live.
- Hit-shade text warns of about a minute compiling on first enable. ABI/defaults/presets unchanged; shader requests +0.
- Validation: build + 132/132 CTests passed; schema regenerated. PP diff main: 236/236 unchanged, 0 compiles; diff check clean.
- No game launch or git metadata writes. Layer restart required; S8 compose attempts and GPU cost remain unmeasured.
