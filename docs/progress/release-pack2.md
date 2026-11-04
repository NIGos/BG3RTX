## Release licence payload and lean layer (branch release-pack2, kept, 2026-11-04)

- Package validates and ships LICENSE and LICENSE-EXCEPTION.md verbatim at its root.
  README states GPL-3.0-or-later with the NVIDIA SDK exception in two lines;
  THIRD_PARTY references both. User licence decision: 2026-11-04.
- Added scripts/build.ps1 -ReleaseLayer; packaging reuses it. Reconfigures build-nodiag
  explicitly with diagnostics OFF and RelWithDebInfo, then builds only the layer at BelowNormal.
  First build passed in 23 s. No game launch, upload, commit or Git metadata writes.
- Real dry run: 227 -> 229 files; missing inputs 1 -> 0; manifest differences 0.
  Receipt: .scratch/release-package/BG3RTX-audit-20261104-pack2/audit.json.
- Synthetic suites: package_licences 8/8, release_install 7/7, diagnostics_package 5/5 PASS.
  Tests use worktree scratch; real saves untouched. Shader request delta 0; ABI unchanged.
- Check offline with build.ps1 -ReleaseLayer and package.ps1 -DryRun -NoBuild -NoZip
  -Version <fresh-version> -OutDir <scratch>. Final archive and release acceptance remain open.
