## Strand holes under a fast camera: stale publish check, cards for that frame (branch claude/hair-stale-publish, READY_FOR_LIVE, 2026-11-10)

- hl1 210357 (VALID): all 5 frame watchdog trips applied the previous build frame's publish (k = 1) with its header
  camera one step (0.34-0.70 m) from the frame's, VP eye 0; static windows 0 trips.
- Cause: under the wheel dolly the game submits some G-buffer passes before their frame's scene build; the newest
  publish ordered before them is the previous frame's (the builds rewrite points and BLASes in place: the only
  consistent one). The pick counter called that "this frame's publish" (any upgrade counts), hence 5z's "58 of 61".
- The brief's option (b) already exists (hp_rebase: rays start at gCam.position - header camera). The trips are its
  first non-zero use and it lost the strands; why is not decidable offline. Option (a), waiting for the frame's build,
  would block the render thread on its own later submit.
- Fix (CPU only, no shader, no ABI, variants delta 0), key `rt_hair_stale_publish` (off): at the G-buffer's submit the
  applied publish's build camera is compared with the G-buffer's own camera (its gCam UBO +208, trusted when its VP eye
  is at the origin; else its frame's build camera). Over 1 cm (hp_on_card's fixed band term), or unknown for an older
  publish, every recorded slot draws its card that frame (existing card mode; no coverage evidence, no watchdog
  judgement). One card frame instead of 28. Key off: counted and logged only.
- New lines: `stale publish check (last N frames)` per period and the first 8 `stale publish: G-buffer frame F ...`
  (offset, camera source, UBO VP eye, UBO camera against the build camera of the same frame).
- CTest hair_groom_asset_test test_stale_publish. hl1 regenerated with the key on (predicted.json fix_6a_stale_publish):
  expect_every watchdog trips 0 in lower and watch, the stale check line acting in lower; lint 0 failing (live, frozen;
  one readiness query appended to lint_corpus_snapshot.json).
- Live hl1 should show: 0 frame watchdog trips; "camera moved since the publish" >= 1 and "drawn as cards" >= 1 in lower;
  0 moved in static windows. The UBO-vs-build camera distance then says whether the rebase (Y about 0) or +208 is wrong.
- User check (once qualified): S8, zoom the wheel fast; the hair keeps its strands except at most a single card frame.
