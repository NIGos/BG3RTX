## Raw compressed LS replay (branch ls-pack2, offline verification, 2026-11-05)

- Inn log: four replay lines; only blocker was decoded BC4/BC5 exactness.
  No other replay warning was present. The failed capture published no pack.
- Diagnostic sampled BC1-BC7 allocations now carry TRANSFER_SRC, including
  BC4/BC5 UNORM/SNORM. Raw copies use bound mip/layer ranges and block rows;
  manifest/replay retain VkFormat and component swizzles. LS rejects decoded BCn.
- Decoded compatibility reads remain available for other passes.
- Synthetic tests cover odd sizes, partial blocks, nonzero mip/layer bases,
  multiple layers/mips, lossless file bytes and sampled-value identity.
  Production LS fixture samples a BC4 atlas and BC5 cookie with control runs.
- Disk cap remains 2048 MiB, including manifest and codec overhead. Block writes
  now check the remaining budget before writing; undersized/exact-fit tests added.
- ABI unchanged at 27; shaders/variants.txt request delta 0; no LS shader change.
- BelowNormal build.ps1 -Test PASS 195/195 (166.33 s CTest).
  Log: .scratch/ls-pack2-final-build.log. Raw byte/range differences 0 across
  110 upload pieces; sampled differences 0. LS fixture PASS in 12.91 s.
- Synthetic pack directory: 4,610,652 bytes, below 2,147,483,648-byte cap.
  Codec cap tests pass: pre-write refusal, header accounting and exact fit.
- Offline only: no game launch, queue mutation or git writes. No GPU cost claim.
  Real inn pack size and live-reference equality still require a future capture.
