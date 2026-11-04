## Reflection and local-shadow optimization round 1 (branch opt-rflsh, rejected, 2026-11-05)

- No candidate retained. Production code/settings/ABI unchanged; request delta 0.
  No commit, game launch or queue submission.
- Reviewed tonight's four `opt-profile-*.log.profile.md` tables. Inn is the static Ultra
  maximum for both passes: reflections 2.115 ms (2.110-2.119, 2 periods), local shadows
  2.028 ms (2.027-2.029, 2 periods). These are baseline costs, not optimization results.
- Candidate #2, receiver/probe early-out: dropped because the required ON output proof
  is unavailable. `common/replay_pack.h` kPasses only supports gi_hit_direct and the
  water compose fixture; lighting inputs alone do not make frame_replay a reflection
  replay. `rt_reflections.hlsl:433-437,486-495` still evaluates zero-weight arithmetic, clamps
  specular and applies a vector-wide finite fallback. Skipping ambient evaluation
  must establish the same result for NaN/Inf/overflow, negative specular and every
  model/roughness endpoint; w == 0 alone is insufficient. Roughness rejection already
  prevents rays at :232-234. Audit estimate 0.05-0.25 ms remains unverified; retained
  saving 0 ms. No key or live job retained for a candidate without its offline proof.
- Candidate #8, identical visibility reuse: dropped for insufficient evidence of a
  saving above 0.05 ms. Inn log configuration is K1 + one remainder, confirmed active.
  `common/ls_exact_k.h:lk_add/lk_end` partitions list entries into disjoint exact and
  remainder sets. With unique light IDs, these two slots cannot repeat a query.
  Duplicate light IDs across different entries are possible but not counted in these
  logs; zero duplication in the captured game frame is NOT claimed. For two remainder
  samples, `rt_local_shadows.hlsl:1337` changes q == 5's stochastic key, so matching IDs
  alone cannot justify reuse under soft shadows. No speculative cache/register cost
  added. Audit estimate 0.01-0.15 ms only applies to duplicate-heavy configurations;
  no supported positive estimate for tonight's profile. No key or live job retained.
- Other early-outs: `ls_light_traced` already rejects range/channel/cone/normal failures;
  exact-k rejects zero/non-finite contribution (:1322). Empty lists reach `ls_finish`;
  its AO/direct-sun giveback, clamps and finite fallback can matter. A sun shadow cannot prove local lights
  contribute zero. No safe additional zero-output shortcut demonstrated.
- Ray flags: binary `rt_visibility` already uses ACCEPT_FIRST_HIT_AND_END_SEARCH
  (`rt_common.hlsli:1133-1139`); these are inline queries without a closest-hit shader
  invocation to skip. Reflection nearest-hit queries must retain nearest-hit semantics.
- Validation: `scripts/build.ps1 -Test` built successfully; 174/175 passed. The water
  shader gate could not open the shared SQLite cache under workspace permissions;
  its sole retry passed (0.79 s) using a local copy of the water cache blobs/index.
  All 175 tests passed across these runs. Used /Z7 and /DEBUG:NONE /INCREMENTAL:NO.
  Removed oversized generated SDK copies/link intermediates; no file > 20 MB retained.
  Equivalence/job lint inapplicable (no shader edit/job); no energy/flicker pass claimed.
- HEAD/main identical (`rev-list` 0/0); `git merge main` denied at shared ORIG_HEAD lock. No refs changed.
