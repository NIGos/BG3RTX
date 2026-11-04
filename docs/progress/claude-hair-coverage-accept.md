## Hair coverage: accepted strand pixels, unique card pixels (branch claude-hair-coverage-accept, READY_FOR_LIVE, 2026-11-04)
- P0: replaced stacked prepass occlusion samples with unique card-pixel tags in the G-buffer shader.
  Probe and swapped draws use the same marker planes; removed card queries, precise-query requirement,
  private probe header, cross-pass CoveragePairing and native-query tracking hooks.
- Draw attribution: native FS inputs have no index-range key, and misses cannot identify a groom.
  Specialization 1102 supplies a compact draw-key slot (16 total), stable while live; a staged
  groom-to-slot map groups shared keys. Overflow keeps cards/no probe/no swap; coverage logs
  `draws without a slot N`. shaders/variants.txt request delta: 0.
- P1: accepted marker is written only after groom, segment, ownership, UV and tip rejections.
  The sealed ray cache remains reusable; refused/other/traced diagnostics keep their original rules.
- P2: duplicate applied tags immediately invalidate evidence; ambiguous readback is rejected and logged.
- P3: latest out-of-bounds evidence blocks later swaps; delayed older successes cannot replace it.
  Log distinguishes latest ratio from min/max used for actual swaps in every pass; untruncated
  per-key line: `[hair primary] coverage used %016llx: ratio %.6f..%.6f; %u swap decisions`.
  Supplied hv7 period totals cannot reconstruct the evidence used for its four eab0 swaps.
- P4: frame/key depth latch chooses native GREATER_OR_EQUAL + depth-write fallback when strands refuse,
  also for supported native fallback pipelines whose strand clone refused. Sibling draws retain the latch.
  Unresolved keys, including absent draws or unavailable native fallback, remain true orphan diagnostics.
- CPU regressions replay prepass -> G-buffer -> delayed readback; cover ambiguity, bounds exit,
  old success, native fallback/zero supported-path orphans, stacking, rejection and overlapping keys.
- One packed card/accepted pair (8 B/pixel) plus 16 B cache: width * height * 24 B,
  65.6 MB / 62.5 MiB at 2560 x 1067, independent of groom count (formerly 208.4 MiB at 8).
  Pipelines per supported native G-buffer: 16 strand + 16 probe, formerly 64 + 64.
  Unswapped prepass depth gives one front-layer owner; swapped overlaps use last writer for both words.
  Count masks tags to 24 bits and requires equal packed words for acceptance. Named offsets asserted.
  CPU tests cover stable/reused slots, 17th-key overflow, four tags, overlap, rejection and tag wrap.
  Allocation beyond 32-bit count offsets keeps cards. GPU cost remains unmeasured.
- Targeted helper: 3/3 requests compiled and spirv-val passed (2 rebuilt, velocity cached).
  No extra variants.txt requests exist for these files. No shared shader include changed; ordinary
  shader source closures are unchanged, so no full identity run was requested.
- Full build/CTest: passed, 149/149 CTests (121.24 s), including hair GPU and host_atomics_lint.
  BG3RTX_SHADER_CACHE points to this worktree's build/shader_cache; external shared cache is unwritable.
- Live: not run. Keep rt_hair_groom_primary off in presets; kCoverMin/kCoverMax and ABI unchanged.
  On Dialogue1 close-up, require every-window ratios 0.891..1.094, used ratios inside bounds, zero
  orphan depth drops, visible complete hair, and GPU cost; then smoke_ultra. No queue files were touched.
