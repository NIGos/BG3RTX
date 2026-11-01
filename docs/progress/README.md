# Progress fragments

Branches write their progress entry here, not in `docs/PROGRESS_CODEX.md`: one file per branch,
`docs/progress/<branch>.md` ("/" becomes "-"). Format: one or more `## <what changed> (branch <name>,
<status>, <date>)` sections with short bullets (what changed, key numbers before/after,
kept/rejected/paused, how the user checks it), at most 40 lines, no log dumps.

After merges the coordinator runs `py -3 scripts/fold_progress.py` (check only: `--check`), which
moves the sections to the top of PROGRESS and deletes the folded fragment files. This README stays.
