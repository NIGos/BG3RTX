## Player docs: three claims corrected (branch claude/fix-docs-claims, docs only, 2026-11-23)

- Ctrl+Shift+F7 only flips the live `rt_hair` key (`layer/util.cpp` 301-311): it switches RTX Hair off and back on
  after the launcher turned it On, never on from Off. README, packaged README, USER_SETTINGS, release notes, evidence,
  Nexus description and changelog now say so.
- Faster bounce light tracing and Bounce trace reordering act only inside the hit-GI trace, so only with Off-screen
  bounce light Near or Far (Ultra by default); on Low, Medium and High they do nothing. USER_SETTINGS says so.
- RTX Hair after a load: about 100 frames (96 in r4-inn, 116 in hx1a, 2026-11-21 logs), not about 70. Release notes,
  evidence and Nexus changelog corrected.
- Not changed (common/settings.h, being edited by another agent): the RTX Hair description shown by the launcher
  ("Turns on and off while you play, also with its in-game key", line 460) should say the key only switches it off
  and back on after it is On; the Faster bounce light tracing and Bounce trace reordering details (lines 338, 348)
  should say they act only with Off-screen bounce light Near or Far.
- The shipped README and USER_SETTINGS changed: repackage and rerun verify_rc before publishing.
