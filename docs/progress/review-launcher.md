## Launcher defect review (branch review-launcher, PASS offline, 2026-11-06)

- Fixed runtime paths containing `#`: INI output quotes them; parsing preserves hashes and literal Windows backslashes. Regression reproduced truncation before the fix.
- Fixed web shortcut capture: Tab traverses normally, repeat events do not rebind, and shifted digits map to the host's digit keys. Each regression failed before its fix.
- Fixed shortcut repair deadlock: editing/resetting one binding no longer requires the other saved binding to be valid. Full-pair validation still gates Save. Two bridge regressions failed before the fix.
- Both GUI launch paths now display the game DLSS guard's repair/error note instead of discarding it.
- Corrected README/USER_SETTINGS: game graphics repair and backup, opt-out, runtime versions/discovery/staging. No settings defaults changed.
- Shader identity: 218/218 byte-identical to HEAD b9377fe237a4 by identical input keys; variant request delta 0.
- Final BelowNormal `scripts/build.ps1 -Test`: PASS 197/197 (10 s build, 252.82 s tests). Settings: 4,165 checks pass. Regression fixtures failed before their fixes. `git diff --check` passes.
- No game launch, queue writes, Git writes, ABI edits or changes to the other lane's files. Logs: `.scratch/review-launcher/`.
- Build environment: prepended the installed VS Installer directory to process PATH for vcvars64's `vswhere.exe` lookup; no build-script change.
- Unconfirmed: concurrent presents on different Vulkan queues may race on `ab_poll_hotkey` static edge state (`layer/util.cpp:224`); no multi-queue reproduction, no speculative fix.
