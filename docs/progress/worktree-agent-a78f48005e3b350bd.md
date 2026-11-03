## Native hair prerequisites (branch worktree-agent-a78f48005e3b350bd, READY_FOR_LIVE, 2026-11-03)
- Resolved main integration in gi.cpp, water.cpp, shader_variant_test.cpp; regenerated variants.txt, never hand-merged it.
- Preserved main migration, RF GI layouts, pool variants, water glint/MV and async background pipeline builds.
- Worker closures own specialization values; their complete IDs/values join build and installed-slot keys.
- Preserved main pipeline timing/spec logs; layout destruction parks pending builds, including new water MV/glint layouts.
- Water hair uses existing RT_SPEC_NATIVE IDs 1008=0 / 1010=scalp capability; physical origin 1009 remains GI-only.
- No new shader key or SpecId. No new on/off defines. Existing water record/binding defines stay unchanged.
- Manifest: 165 -> 167 requests (+2 explicit base water spec requests), 53 GI bases. No shader source changed.
- Key-off: all 251/251 main requests byte-identical by input keys, including removed manifest spellings.
- pp-diff main f0b0ccbb2733: 209/209 common requests identical inputs; 0 compiles needed.
- New waterhair gate sample 16: 19/19 requests, 64 entry/assignments, control 9,194 differing lines.
- Sample 8 passed: cascade 44/44 (control 1), guards 26/26 (436), hitfam 23/23 (17,633), gifar 12/12 (98,934),
  hitinst 23/23 (52,940), native 64/64, 332 comparisons (54,005); rfgi 12/12 (129,638), wateredge 9/9 (19,629).
- All 9 sampled gates passed, 0 failed; every control non-zero. No comparison rules weakened.
- Comparison AST unchanged: block_order, phi_order, canonical, differ, check_one (5/5).
- Added CPU coverage for owned async payload/copy lifetime, distinct hair/edge keys, water request/reference mapping.
- Final CMake build passed; CTests 118/118 (343.45 s), including the new CPU coverage.
- hair_qual generator/expect/prediction now require RT_SPEC_NATIVE and built spec hair=1; offline dry run passed.
- Dry-run environment first lacked py; rerun with a temporary launcher shim passed, no project tool changes.
- No Git metadata writes or game launch. No ABI/default/preset change. GPU ms and live quality unmeasured.
- Live status remains READY_FOR_LIVE, not authorized: S8 water + Test Dialogue1-3, smoke_ultra + hair startup latches.
- Live gates still pending: coverage participation; invalid/missing and retired bounds-invalid 0; scalp hairline coverage;
  origins matched/available/unresolved; character ROI ON >= OFF - spread; primary matches; GPU cost; water drawn;
  no skipped variant, scene-wait timeout or measured-window build; first-enable longest present interval < 1000 ms.
- In game after authorization: native hair should brighten dialogue characters without a startup freeze; water remains drawn.
- Logs: build/merge-hair/merge-hair-{build-final,ctest-final,pp,identity,dryrun,spec-*}.log. Keep features experimental until live gates pass.
