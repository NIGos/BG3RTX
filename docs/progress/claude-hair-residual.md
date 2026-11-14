## RTX Hair: the dolly loss named (the decal pass took the header's label), key rt_hair_header_scene_pass (branch claude/hair-residual, READY_FOR_LIVE, 2026-11-14)

Design and evidence: docs/RTX_HAIR_PRIMARY.md 6h. Offline only; live hx1 180928 analysed, no new live.

- hx1 180928 (S8, 12 of 12 phases, INVALID for two job faults). Card fill on: frame watchdog 0, key watchdog 0,
  coverage low 0 in all five windows, 0 frames on cards (6g's gate held); 12 and 10 reported trips in the two dollies.
  Fill off: 6 trips, 10 switches to cards, 28 of 61 frames with every draw on cards.
- Dense samples, card-marked pixels: still 84-88% accepted, 4-5% no strand; moving 53-63% accepted, 25-37% no strand.
  All 600 lines: origin 0, 0 mrad, anchors still (median 1.3 mm), strand of another draw 0. The loss follows camera
  step x sin(angle from the view axis): accepted 0.79 under 0.1 m, 0.16 at 0.3-0.5 m, 0 above 0.5 m.
- Candidate named: P5 (acceleration structure). Points, BLAS primitives and rays are of the frame, the TLAS is the
  publish before (boxes one camera step old). P1-P4 and P6 are out.
- Cause: the header is applied once per frame label at the first pass with 4 or more colour targets on the G-buffer
  depth. The decal pass (4 targets) matches too. When the present falls between the scene G-buffer pass and the decal
  pass, the decal pass takes the next label and the next frame's hair runs on the header it applied: the old TLAS
  and rows over BLASes, points and camera of the new build. The build stamp was copied at that apply, so 6e read
  "same build". Rows read on the GPU in 11 frames are those of build label - 1.
- Kept, off: `rt_hair_header_scene_pass` (Advanced knob, live, CPU only): only the scene G-buffer pass (5 targets)
  applies the header; velocity draws accept the label of the interval before. Not a pure fix (it changes the label
  the velocity draws and the count see in that phase), so a key. No shader, no ABI, variants.txt delta 0, GPU cost 0.
- Proof offline: CTest hair_groom_asset_test test_header_pass (present between the two passes: key off 59 of 60
  frames trace the publish before, key on 0, no velocity draw refused; present elsewhere: no change).
  build.ps1 -Test 226 of 226.
- Open: two scene passes in one present interval (2 of the dolly's 16 frames, after LOD decode hitches of 47-94 ms):
  the second runs on the first one's header and reuses its entries, with or without the key. Counted on the new
  `[hair primary] header pass` line. Needs a header and tag per scene pass recording; not done here.
- Diagnostics: dense sample lines 96 per stats period instead of 600 a session (the first dolly used them all).
- Card fill into the On bundle: gate held, cost in the noise (0.30 against 0.39 ms, 4-7 frames), still image 4-5% of
  the card pixels show the card, no hole in the 1280 px PNGs, seam not judgeable there. Its own commit on branch
  `claude/hair-fill-bundle` (user decision; not needed for the dolly once the key is in).
- hx1 regenerated (predicted.json first): dolly windows of 180 frames (the 60-frame out window ended before the
  tenth notch), sample pattern allows `camera step -1.000`, PNGs only in still windows at full width. Key on without
  and with the fill, then key off, in one launch (about 110 s of phases). Gate with the key: no label taken by the
  decal pass, and in the four dollies frame watchdog trips 0, no frame on cards, no watchdog or low-coverage switch.
  Key off: label taken by the decal pass 1 or more. Lint 0 failing (frozen), snapshot appended only.
- User check after a go: S8, RTX Hair On, Advanced "Strand hair: header at the hair's own pass" On; zoom in and out
  fast with the wheel: far heads and heads at the screen edge keep their strands.
- Next: the live; if it holds, the key joins the On bundle; then the hitch frames (per-recording header, LOD decode
  off the render thread).
