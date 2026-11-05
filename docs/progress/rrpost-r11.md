## S-F05 R11G11B10 boundaries (branch rrpost-r11, fixed offline, 2026-11-06)

- Confirmed: `shaders/rr_post.hlsl:67` masked the FP32 mantissa without exponent limits.
- Built the real Vulkan R11 image store/load fixture before editing the shader.
  Old emulation: 2250 subnormal, 3 finite-overflow and 12 special channel mismatches
  in the initial 214290 comparisons; no normal-range changes.
- Fixed subnormal steps, finite saturation and NaN payloads; +Inf preserved, negatives zeroed.
  R/G maximum 65024; B maximum 64512. No new key, ABI change or shader request.
- Final fixture: 77933 mixed RGB inputs, 233799 exact channel matches; 0 normal changes,
  0 independent numeric-oracle mismatches and 0 Vulkan validation errors (RTX 5090).
- Validation first found installed ReShade loader interference; reused existing fixture
  isolation (implicit layers disabled), then passed. No game or system settings changed.
- Normal-domain and quantisation-OFF release SPIR-V equivalence: both RR requests pass.
  All 213 unrelated requests are byte-identical. Full-range equivalence is not claimed.
- CPU unit tests check the numeric oracle and boundary corpus; fixture is a build-only CTest.
- Full `scripts/build.ps1 -Test`: 196/196 pass (4m23s); final fixture rebuild/rerun passes.
  Updated merge-scope regression: 9 tests pass. Baseline proof: `--baseline HEAD` (5256bec2d25a).
  Logs: `.scratch/rrpost-r11-build.log`, `.scratch/rrpost-r11-final.log`.
- No game launch, queue write or git write. GPU work only with all three games absent.
- Live GPU cost unavailable (offline only). Existing `rr_post_check` is the later replication check.
