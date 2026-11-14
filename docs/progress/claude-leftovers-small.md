## Leftovers landed: licence attributes, two spec tools, three result notes (branch claude/leftovers-small, docs and tools only, 2026-11-15)
- `.gitattributes`: `docs/release/licences/** -text -whitespace`. Licence evidence keeps its stored bytes (9 files
  CRLF, 9 LF in the index); `git status` shows none of them modified after the change, nothing renormalised.
- `tools/spec_omm_alias_probe.py` (from origin/variant-migration) runs unchanged: 3/3 modules valid, control 33
  differing lines, both alias values equivalent.
- `tools/spec_constant_proto.py` (from worktree-agent-a0dba2e7) needed one fix: today's old modules carry the later
  spec constants (1001+), so the old side is now frozen and folded like the new side, and canonical() comes from
  `tools/spirv_equivalence.py` (the doc references say so). Result today: spec 0 = old plain, spec 1 = old cascade
  (873 globals, 24802 body lines, 0 differ), control 4 body lines; DXC 6.3 s for two modules vs 3.3 s for one.
- No runtime, shader, setting or CTest list change. Cost 0 ms.

## ReShade live rs1 was INVALID for its question (branch reshade-compat, still unverified, 2026-11-05)
- rs1 (Water Queens House, `sl_init_after_create=1`): `[compat] ReShade not loaded`. ReShade 6's global layer needs
  a ReShade.ini in the game folder, and it was removed before the run. The ReShade case is still unverified.
- Without ReShade the key was harmless: trigger = the game's NGX Init, `slSetVulkanInfo: ok`, FG 2.00, no mini-dump.
- Next: one rerun with ReShade enabled for BG3 (the user's choice), then default 1 if the gates hold.
  Full text: TRACK_B_FG.md, after the FG settings table.

## Strand density: the tips dither is the measurable coverage loss (branch claude/hair-density, offline, 2026-11-07)
- LOD, converter and radius are not causes. With `rt_hair_groom_tips=1` the strand/card pixel ratio of the main draw
  is 0.931-0.934 against 0.952-0.973 with tips off; small draws 0.79-0.84 against 0.93-0.97 (under the swap bound 0.891).
- Kept: `rt_hair_groom_tips=0` (no code). Full text: RTX_HAIR_PRIMARY.md after 5d.

## Primary-only minimum strand footprint: rejected offline (branch claude/hair-footprint, REJECTED, 2026-11-07)
- Widened strands with a keep-dither: accumulated residual 0.079-0.20 against 0.059 for today's thin single ray,
  mean 0.250-0.329 against the reference 0.332. The DLSS jitter already stratifies the thin strand's sample.
- Rejected before any build. Table and reasons: RTX_HAIR_PRIMARY.md after 5d. The branch's `test_footprint_rejected`
  was not landed: on main its any-hit check fails (-3.6% against its 3% bound), and the bound is not ours to tune.
