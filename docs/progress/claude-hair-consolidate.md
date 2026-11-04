## Hair strand path consolidated offline (branch claude-hair-consolidate, READY_FOR_LIVE, 2026-11-04)
Readiness only, not live qualification. The failure-mode table (37 hair rows, all closed, 3 accepted limitations)
is in [RTX_HAIR_PRIMARY section 9](../RTX_HAIR_PRIMARY.md#9-known-failure-modes-and-offline-proofs).
Generated-material rows (hgn1/hgn2) are not hair: TRACK_D_REFLECTIONS owns them.

Orphan counter, exact by construction:
- The latch holds a prepass drop of key K at frame p for G-buffer draws in [p, p + 2]. Epochs and the
  deferred resolved/failed sets are gone.
- An orphan is a latched G-buffer draw drawn natively without fallback, counted at the draw. The new line:
  `[hair primary] depth orphans (last 60 frames, first 4; key prefix:prepass frame_index,draw frame_index,reason,pipeline): none`.
- A key with an uncovered sibling pipeline blocks prepass drops for 300 frames.
- hv9 frame 2631 (3 "orphans" with 180/180/180 swaps and 0 fallbacks) cannot be replayed (aggregates only).
  It is closed by construction: tests for a true orphan (1), the uncovered sibling (0), the hv9 sequence (0),
  both recording orders, interleaving and a hitch frame.
- prepass_drop_allowed (common/hair_primary.h) covers the hv7 off-by-one (applied now - 1 .. now + 2).

Review findings (independent read-only review of main 6a176bb0, each confirmed in the code):
| Item | Fix | Test |
|---|---|---|
| P0 LOD upload ready before its copy ran | used only after the copy's recording is submitted and done; else redone | test_upload_submission |
| P0 compaction trusted an unsubmitted size query | query_rec; a discarded query redoes build + query | test_compaction_submission |
| P1 UV join readback | copy recording tracked; discarded job retried | test_uv_submission |
| P1 G-buffer swap without the prepass drop | needs the key's latch in [p, p + 2]; else cards + probe | test_gbuffer_depth_latch |
| P1 velocity without ownership | accepted marker must carry this tag and the key-map slot | hair_groom_test, marker fixture |
| P2 count slots unsubmitted | slot dropped without evidence | test_count_submission |
| P2 tag low 24 bits 0 | count skips such tags | hair_groom_test |
| P2 "strand pixels" were card counts | card and accepted totals logged separately | test_card_accepted_totals |
| P2 refused never reset, mixed with feedback | separate per-period counters | test_period_failures |
Shared helper: readback_state(frame, done, submitted) -> Wait / Drop / Ready.

Other proofs: hair_prefetch_test (every hair prefetch request is in shaders/variants.txt, +3 plain hair requests,
0 new compiles); hair_patterns_test (the hv9 gate regexes match lines rendered from their source formats).
hv9-visual: orphan gates judged at the window end, after the PNG capture.

Verification (reviewer, 2026-11-04): build + 153 CTests; `live-flow: 1 jobs, 0 failing`; check_patterns
101 patterns, 0 missing, 0 forbidden; shader identity vs 6a176bb0: 261/263 byte-identical, the two
differences are rt/hair_primary_count and rt/hair_primary_velocity (key-on only). ABI unchanged.
Not measured: live image, candidate GPU cost (historical hv9 G-buffer 0.197 ms/frame).
