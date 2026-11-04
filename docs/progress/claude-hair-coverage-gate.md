## Per-draw strand coverage (branch claude/hair-coverage-gate, READY_FOR_LIVE, 2026-11-04)

- G-buffer/velocity and the extra prepass rule now require fresh per-key precise occlusion samples. Cards remain
  until proven; failed ratios recover without strikes or retry waits. Pixel-record feedback is diagnostic.
- Native card query plus a store-free groom trace probe in the camera prepass. Query reset and probe header update
  run in that game's command buffer, outside the pass. Actual successful submission, recording generation,
  completion and both availabilities gate readback; partial keys, old scenes and active game queries fail closed.
- `rt_hair_groom_primary` only; no new key/ABI. Off at device startup creates no primary resources or variants and changes no draws.
  `occlusionQueryPrecise` is requested at device creation when supported (rtscene); if absent: UNAVAILABLE once, cards.
- Strands on screen only in header order G-buffer (`rt_hair_groom_primary_order=1`): in prepass order the G-buffer can
  read another frame's header than the probe proved (hc2: 296/300 adjacent tags, player half bald), so cards stay.
- Strand G-buffer timestamps reset in the G-buffer pass's own command buffer (hc2 had no GPU cost line).
- Bound 0.8913102349775665 = minimum offline ratio 0.9105471772550376 - modeled count noise 0.01923694227747121.
  Regenerated 877 passing styles / 2,631 views, zero failures; Minthara LOD1 side is limiting. See section 5b and
  claude-hair-coverage-bound.json. Noise assumes independent spatial samples; temporal noise is unmeasured.
- New shader request +1; variants.txt delta 0. Existing primary G-buffer/velocity/count: 3/3 byte-identical to
  HEAD 3b69d480da6d by identical inputs. Probe: 1/1 compiled and SPIR-V valid through shader_identity.py.
- Probe estimate: 0.025 ms per 100k card fragments / 0.4 ms per 1.6M, plus duplicate vertex/raster/query work.
  No numeric strand G-buffer GPU lines exist in supplied hc1/hc2 logs, so a measured comparison is unavailable.
- Offline validation: build.ps1 -Test passed 147/147 CTests (113.23 s); 25 new gate/bound assertions.
  Build 20 s, BelowNormal, SDKs from main; shader cache local to this worktree. No commits or game launches.
- In-game confirmation remains unrun: camp player/Minthara hair should retain cards below the bound; the exact
  300-frame coverage line must show fresh ratios and swapped-frame counts. Strict native depth tests may keep
  cards permanently; native comparison is preserved. One reset-owning prepass recording per frame, up to 128
  draw pairs; additional recordings/overflow keep cards. Empirical cost and temporal/dither noise remain unmeasured.
