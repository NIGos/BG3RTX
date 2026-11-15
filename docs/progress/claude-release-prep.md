## Release 0.9.6-beta prepared offline: audit, notes, candidate, staged final smoke (branch claude/release-prep, READY_FOR_LIVE, 2026-11-15)

- User decision 2026-11-15: finalise for release, RTX Hair Off by default. Nothing uploaded, tagged, installed or queued.
- Audit of `common/settings.h`, `bg3rtx.ini`, `data/release.ini` and `enforce_release_policy`: no inconsistency found,
  no settings change. A release player sees 23 controls, 4 more behind "Show experimental options" (Water surface
  reflections, two Frame Generation pacing tests, Strand hair edge resolve); 32 developer knobs stay hidden.
- RTX Hair: Off in Low, Medium, High, Ultra and without a user file; a Lighting control with the Beta badge; On keeps
  all 26 bundle keys under the release policy in every preset with experimental options 0 and 1.
  `test_release_rtx_hair` now pins this (it covered Ultra with experimental options 1 only).
- Diagnostics: `rt_hair_dense_sample` pinned 0 by release.ini; `rt_hair_card_fill` is a hidden developer knob reset
  to 0; `rt_hair_groom_async` and `rt_as_chunk_fit` are raw keys at 0 with no control.
- Presets: Low-latency scene and RTX Hair motion stability On in High and Ultra; the four VRAM policies 1 everywhere.
  Without a user file the package resolves to High without OMM and masks (the launcher writes a preset on first use).
- Version: 0.9.6-beta (the repo's published names carry -beta); `tools/launcher/version.h` was already 0.9.6.0.
- Docs: `docs/release/RELEASE_NOTES_0.9.6.md`, `RELEASE_EVIDENCE_0.9.6.md` (source of every number),
  `nexus/changelog_0.9.6.txt`; player README, USER_SETTINGS, Nexus description and sticky post updated.
- `verify_rc.ps1`: the payload guard ran `python` (Store alias, exit 1), now `py -3`; default file count 230 (was 223);
  the hand-written readiness section of RC_CHECK.md survives a rerun.
- Candidate from commit 976df3df, lean layer: `dist/BG3RTX-0.9.6-beta.zip` (230 files, 74.0 MB),
  `-symbols.zip`, `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe` (57.7 MB, unsigned), all in the main
  checkout with `SHA256SUMS.txt`. Hashes in `docs/release/RC_CHECK.md`.
- Verification: verify_rc 11 of 12 PASS (build, archive, manifest 230, hashes, exclusions, licences, version, install,
  update, uninstall, cleanup). Setup row FAIL only because the user's install owns the Apps entry: the compiled
  setup was never executed. Extra checks: 0 granny2, licence files byte-equal, release pins effective in the packaged ini.
- Final smoke staged in `tools/live_batches_staged/release_final/` (predicted.json first): `final-s8-ultra-high`
  (Ultra then High at S8, about 3.8 minutes) and `final-stock-hair-dialogue1` (about 3.4 minutes). Frozen lint and
  live preflight 0 failing; snapshot +3 queries, append only. New gates: 0 handout fallbacks, 0 dropped opaque draws.
- Tests: `scripts/build.ps1 -Test` 237 of 237; merge_scope tooling exit 0. `shaders/variants.txt` delta 0, no ABI change.
- Open, in RC_CHECK.md "Not verified": no live in the shipped profile since 2026-11-14 (blocker unless the user accepts
  their own hairtest18 sessions); Setup.exe never run on this PC (blocker for publishing the setup, not the ZIP).
- Stale elsewhere, not edited here: HANDOFF says RTX Hair is "hidden in release"; it is a visible Beta control.
