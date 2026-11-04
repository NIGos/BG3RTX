## Runtime classification tables included (branch pkg-runtime, kept offline, 2026-11-04)

- Fixed release packaging: include `shaders/rt/alpha_fs.txt` and `camera_fade_fs.txt`.
  Missing alpha classifications made foliage opaque in ray tracing in the release.
- Audited layer/core install-relative reads. Asset DB, settings, shader requests/includes,
  core, DXC and production Streamline inputs are covered. `vt_alpha` is optional:
  no indices disables offline masks; `hair_grooms` absence retains the card path.
  Captures, caches, developer paths, optional Aftermath and SHaRC headers stay excluded.
- Added one registered runtime package guard deriving 154 dependencies from source,
  checked against the actual dry-run plan. Removing either classification table fails.
  `verify_rc.ps1` runs it before passing the Manifest row.
- Payload expectations: 219 -> 221 files. Requested dry run: 221 files, zero manifest
  differences, 13 missing build inputs in this worktree; this is not full RC qualification.
- Validation: runtime guard 1/1, licences 8/8, install 10/10, setup 4/4 passed.
  Shader request delta: 0; runtime/GPU cost unchanged.
- In-game follow-up after an authorized release rebuild: check foliage cutout shadows
  and camera-fade classifications. No game launched or installed copy modified here.
  No commit created.
