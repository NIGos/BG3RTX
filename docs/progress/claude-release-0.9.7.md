## Release 0.9.7-beta prepared (branch claude/release-0.9.7, merge pending, 2026-11-24)
- Why: 0.9.6-beta is published on Nexus (built from b7532c90). On 2026-11-24 main 7ee3f9cf was rebuilt under the
  0.9.6-beta name by mistake and two lines were added to the 0.9.6 changelog. The user chose 0.9.7-beta for the fixes.
- Version: `tools/launcher/version.h` 0.9.7.0; README.md, docs/release/README.md (packaged player README), Nexus
  description and Nexus README name `BG3RTX-0.9.7-beta-Setup.exe`. History mentions of 0.9.6 stay.
- Docs: `RELEASE_NOTES_0.9.6.md` and `nexus/changelog_0.9.6.txt` back to their state before ace1ebd6 (the two lines
  removed; the 2026-11-21 doc-claim corrections stay). New `RELEASE_NOTES_0.9.7.md` and `nexus/changelog_0.9.7.txt`:
  level recognised from the game's lights after Patch 8 (Medium, High, Ultra; about 0.2 ms), fresh-install Play saves
  the preset, restart notices for Hair shadows and Detailed logging, 71 of 71 cutout masks (about 36 MB).
- Sticky post: "Version 0.9.7 - public beta", short "New in 0.9.7" plus a one-line 0.9.6 summary; 4473 characters.
- Mistaken set moved to `.scratch/old_rc_7ee3f9cf_misnamed/`; the new Nexus set is `.scratch/release/v0.9.7-beta/`
  (hashes in RC_CHECK.md).
