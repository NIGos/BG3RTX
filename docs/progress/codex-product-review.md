## Release product review (branch codex-product-review, design ready, 2026-11-02)

- Added `docs/release/PRODUCT_REVIEW.md`: source inventory, ranked findings, original branding,
  per-user Inno setup design, launcher layout, diagnostics privacy, first run and error states.
- Current UI is the Win32 launcher; own in-game settings overlay and Diagnostics are planned only.
  Current table has 8 player Experimental knobs, versus 4 in USER_SETTINGS and 5 in progress.
- Release risks: partial upgrade recovery, package/data provisioning disagreement, missing required
  notices, saved versus active state, choice-level experimental/developer visibility.
- Three future implementation streams have exclusive ownership and acceptance checks.
- Docs only. No builds, CMake, CTest, DXC, game, installer or launcher execution. No Git writes.
  No saves accessed. Shader request delta 0; no runtime/GPU cost change.
- Visual inspection remains pending: captures/ is absent in this worktree. Review UI screenshots
  and disposable install/report fixtures when implementing; rendering lives need separate steering.
