## Launcher GUI review fixes (branch gui-review-fixes, offline verification, 2026-11-03)
- Fixed all 13 review findings; strand primary activation is latched at device startup and requires restart.
- Hair capability/local groom checks, startup prerequisites and Experimental authorization are enforced; unqualified controls stay in Advanced and off in presets.
- Added water Combined (7) and shading prerequisites; crowd labels use segment budgets; FG badges follow Off/on transitions.
- Disclosures and report focus survive refresh; sliders expose Off and units; card copy and costs use central metadata with Details.
- Validation: full build and 146/146 CTests passed (194.04 s); settings 4,356 checks, bridge 904; stale count corrected; defaults/schema and diff checks pass.
- Kept; ABI and shader request delta 0. No game launch or Git metadata writes; WebView2 zoom, high contrast, Narrator and DPI acceptance remain manual.
