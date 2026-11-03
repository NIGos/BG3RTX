## Manual candidate trial (branch user-trial, READY_FOR_USER_TRIAL, 2026-11-03)
- Eight normal Experimental controls: strand hair, native hair + scalp, pool trim,
  cheap hair coverage, RR resolve/order, water camera correction and hidden-hit shading.
  No default promotion; RR remains before post by default; GI mean stays quarantined.
- Ctrl+Shift+F9 flips the last saved Experimental choice; native hair/scalp flip together.
  No notification renderer exists: feedback is one `A/B: key=value` log line.
- Live hair enable reserves 88-byte capture vertices and supported optional LSS at startup.
  Extra capture storage is 24 B/vertex versus 64 B, or 64 B versus 24 B; cost unmeasured.
- Settings test: 3475 checks, 0 failed. Defaults generator current. Build and 132 CTests pass.
  Shader request delta 0; ABI unchanged. No game launch or Git metadata writes.
- Prior costs, saves, visual checks and prerequisites: USER_TRIAL_20261103.md.
  Live behavior and startup preparation cost remain unverified by this branch.
