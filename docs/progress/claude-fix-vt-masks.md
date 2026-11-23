## VT alpha masks: all 71 load again; OMM latch visible per window (branch claude/fix-vt-masks, ready to merge, 2026-11-23)

- Cause: LSLib pads a mip level below one tile (128 px) to the tile with opaque blocks. `tools/assets/vt_alpha_masks.py`
  stored those padded levels, and the layer's `parse_mask` (common/vt_alpha_masks.h) needs the exact chain.
  So 17 of the 71 masks of 2026-11-02 were rejected at every launch (`[vt alpha] ... invalid: skipped`). They cover
  309k of the 782k camp2 triangles, which stayed on the page-cache alpha path. This predates 0.9.5.
- Tool fix: each level is cropped to its exact mip size (`crop_bc4`; the content is the top-left blocks, and
  every outside block is the opaque pad, checked on 1_1ee1...).
- `--repair <dir>` crops existing padded files in place, bit-exact, without the game's paks.
- Shared data: the 17 files in `data/assetdb/vt_alpha` were rewritten with `--repair`. Count before and after:
  54 exact + 17 padded, then 71 exact. The asset DB still holds 998 files. Loaded masks: 43.8 MB (54) before, 80.1 MB (71) now
  (+36.2 MB of VRAM, BC4). On disk the 17 files lost 4-22 KB of pad each. `vt_alpha_manifest_camp2.json` still lists the old byte sizes (informational only;
  the layer reads the index).
- Tests: `vt_alpha_masks_tool_test.py` (crop, observed padded sizes, repair, folder writes only padded files,
  idempotent). `vt_alpha_masks_test` now also parses every indexed mask of `data/assetdb/vt_alpha` with the layer's
  own parse_mask (71 of 71). It skips when there is no asset DB.
- OMM guard (finding 4 of the layer hunt): when `retained` hides the `[omm]` lines, each window now logs
  `[omm] off for this session: retained until the device is destroyed`. Layer change, no ABI change.
- User check (next launch, High or Ultra): `[vt alpha] 71 masks loaded` and no `invalid: skipped` lines. In camp,
  `VT records per frame read an offline mask` should rise.
