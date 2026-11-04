## Water compose binding and lifetime gates (branch water-binding-fix, offline verified, 2026-11-04)

- wos1 16:09:16: spec 2 selected 0/60 passes; light/input refusals 60/57.
  Forward F was checked against the deferred F-1 stash, first at exposure b22.
  Those are different pass contracts, not descriptor-set handle comparisons.
- Observe the complete deferred tuple at dispatch pre, before the delayed flush.
  F-1 borrowing requires all nine resource descriptors and the game set to be
  re-observed in F; missing/rotated resources still refuse. Keep scene/material
  retention, the one-frame bound and exact build-camera shift checks.
- Forward equality now controls shared WATER_LS bindings only. On a mismatch,
  bind the complete deferred tuple and preserve native local-shadow energy.
  Same-frame hit shading no longer depends on forward descriptor equality.
- Core-only build passed. Ordering regression covers all nine rotations, changed
  game sets, incomplete snapshots, forward-before-flush and missing camera shift.
  All seven water CTests pass; water compose reports 1,203,994 checks.
  Water surface shader CTest passes (492 s with a fresh worktree-local cache;
  the read-only shared cache initially failed to open).
- No shaders, variant requests (+0), settings, presets, host ABI or layer changed.
  No Git metadata written and no game launched. GPU cost remains unmeasured.
- Live confirmation remains pending: S8 with rt_water_hit_shade=2 and
  rt_water_compose_shift=1 should select spec 2/flag 64 when the deferred lifetime
  witness matches, with nonzero qualified hidden/offscreen hits. Rotating deferred
  tuples remain conservative fallbacks. The old log does not include descriptor
  values, so the precise resource/offset/range difference is not established.
