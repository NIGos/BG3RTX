# rt_vt_alpha_masks OFF / ON / OFF at S3 camp (live_batch vt-alpha), NOT authorized

Gates the offline VT alpha masks on their own (docs/P4_OMM.md 11.2), before any OMM work. The key is live: the
masks load and upload at the first alpha commit with it on (`warmup-on`), then matched VT records read them.
Lean format: foreground, FG 2, static camera, smoke_ultra startup, `rt_stats` only in `diag-*`.

Plan (123 s, 3 PNG): `warmup-on` 10, `diag-on` 15 / `diag-off` 15 (rt_stats: the shadow and visibility
alpha-candidate lines), `cost-off-a` 20 + `shot-off-a` 6, `cost-on` 20 + `shot-on` 6, `cost-off-b` 20 + `shot-off-b` 6,
`restore` 5. Exposure held from `cost-off-a`.

Deciding lines:
- `[vt alpha] 7 masks loaded ... (1.65 MB BC4)` and `[vt alpha] last N frames: X VT records per frame read an offline mask
  (rt_vt_alpha_masks 1 ...)` with X > 0 in `cost-on` (expect_every), 0.0 with the key off.
- `rt stats ... alpha candidates of shadow rays`: `vt page from a coarser level` drops in `diag-on` vs `diag-off`, the
  evaluated vt share moves to tex.
- Energy: shot-on vs the shot-off-a/b mean (pre-post colour, dumps.json) within |off-b - off-a| (predicted.json: null).
- Cost: `rt passes: GPU cost`, `gi: GPU cost` ON vs the OFF mean (reported). Scene-wait and capacity gates in every
  cost window; forbid on `held records dropped (view destroyed/reused) [1-9]` (the mask views have no tracker id).

Offline basis (camp pack 013545-2, tools/assets/vt_alpha_agree.py): masks bit-exact where the page is resident;
alpha test agrees on 98.51% of triangle-weighted texel reads, 1.48% differ on coarser pages, 0.01% elsewhere.
Dry run: `powershell -File tools\live_batches\vt_alpha\dryrun.ps1` (2026-11-02: all checks passed, 4.1 min, 0.19 GB).
