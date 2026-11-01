## User decisions: GUI and diagnostics (branch main, decided, 2026-11-01 evening)

- GUI audit (docs/GUI_AUDIT_20261101.md on branch worktree-agent-af4955ea85416c41c) approved as proposed:
  one "RT water" entry with levels; one "Hidden-surface shading" bundle with levels (foliage rows and buffer
  reuse automatic); "Current cascade sun" and exact-k leave the GUI (ini / developer only); "Emission at ray
  hits" writes rt_emissive itself so players can enable it. 28 experimental entries become 8.
- Crash dumps stay off for players. Later: a diagnostic mode in which the user produces a diagnostic package
  (logs, effective settings, system/GPU info, crash dump if any) to attach to a report, together with a save
  the user chooses. The tool may only copy a save the user explicitly selects, read-only; it never reads or
  changes saves on its own (AGENTS "Never").
