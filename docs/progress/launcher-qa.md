## Launcher release QA (branch launcher-qa, fixed with acceptance open, 2026-11-03)

- Fixed visible custom preset reset, native custom detection and choice-specific Apply badges.
- Requirements now show every prerequisite; save review enforces them before persistence.
- Fixed Developer authorization, focus retention, Enter toggles, forced colors and zoom reflow rules.
- Every card states GPU cost or Not measured; flat text surfaces preserve contrast.
- Contrast minimum: token text 6.68:1; Play 5.52:1. Bridge: 695 checks passed.
- Added offline shipped-JS regression plus CSS contrast/reflow contracts to CTest.
- Full build and all 134 CTests passed. git diff --check passed.
- Open: real WebView2 zoom 100/150/200%, Windows high contrast and Narrator acceptance.
- PRODUCT_REVIEW absent locally; see docs/release/LAUNCHER_QA.md for the checklist.
- Check in launcher: review presets, Advanced > Experimental, requirements, Tab/Space/Enter focus.
- No game launch, settings write or Git metadata changes. ABI/shader requests 0; renderer cost unchanged.
