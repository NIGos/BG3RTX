## Launcher UI rework: audit, proposal and two mockups (branch claude/ui-rework, awaiting user choice, 2026-11-14)

- No code change. `docs/ui_rework/AUDIT.md` (23 problems with file:line and severity), `PROPOSAL.md`
  (where each control goes, interaction rules, copy, visual system, 7-phase plan, 7 questions).
- Mockups, static HTML with the real controls of 0.9.6: `docs/ui_rework/mockups/index.html`, `a.html`
  (sidebar, rows, detail panel; recommended), `b.html` (four tabs, grouped cards). 25 screenshots in `shots/`
  (985 KB). Both files come from `build_mockups.py` (one table of controls).
- Designed states: saved, unsaved with review, restart needed, dependency change (RTX Hair off), game folder
  missing, preparation running, experimental switch on and off, light theme.
- Phases 1 to 3 of the plan need no bridge or settings.h change; no host ABI change in any phase.
- How to check: open `docs/ui_rework/mockups/index.html` by double-click. Nothing is implemented until the
  questions in PROPOSAL.md section 7 are answered.
