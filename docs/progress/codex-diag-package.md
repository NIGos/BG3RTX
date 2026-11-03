## Beta diagnostic package (branch codex-diag-package, kept, 2026-11-02)

- Added Diagnostics.cmd / diagnostics.ps1 (Windows PowerShell 5.1), shipped by package.ps1.
- One timestamped zip in the install root: logs, ini files, effective cfg lines, device/OS/GPU info,
  existing crash dumps and optionally one explicitly chosen save. Default: no save, no upload.
- Text profile paths and obvious credentials are redacted. Binary dumps/save remain unchanged;
  README and package manifest explain their privacy limits. Saves are opened read-only; collection
  refuses with a running game. Crash dump settings are never changed.
- Direct Python fake-install gate: 5 tests pass on Windows PowerShell 5.1, including redaction,
  byte/mtime/attribute preservation of a synthetic save, missing files and process refusal.
- No game, real saves, builds or shader tools used. ABI/settings/variant request delta: 0.
- User check: Diagnostics.cmd; use -IncludeSave for the optional file dialog (confirmation defaults No).
