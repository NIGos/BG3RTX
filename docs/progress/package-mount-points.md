## Packaging allows a checkout on a disk mounted as a folder (branch package-mount-points, verified offline, 2026-11-07)

- After the PC reinstall the checkout sits on a 4 TB disk with no drive letter, mounted at `E:\Storage`.
  package.ps1, build_setup.ps1 and verify_rc.ps1 refused every reparse point on the output path, so
  `package_runtime_test` failed ("linked package output: E:\Storage") and no setup could be built.
- Fix: one shared helper, `scripts/linked-path.ps1` (`Find-LinkedAncestor`). Junctions and symlinks are still
  refused; a whole-volume mount point (target `Volume{guid}\`) is a real disk and is allowed.
- Tests: new `linked_path_test` (volume targets, a real junction refused and unlinked safely, the checkout
  allowed); `package_runtime`, `package_licences` (copies the helper into its fixture), `release_setup`,
  `release_install` and `test_registration` pass.
- No shipped file changes (install/uninstall scripts untouched). Cost: none.
- Same cause in the shipped launcher: diagnostics `safe_path` refused the mount point, so a report from a
  game or fixture on such a disk came out incomplete (`launcher_diagnostics_fixtures` failed on this PC).
  It now accepts a reparse point only when `GetVolumeNameForVolumeMountPointW` resolves it to a volume;
  the existing "ancestor junction refused" check still passes. Launcher, package and link tests: 10/10.
