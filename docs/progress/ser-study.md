## SER feasibility study (branch ser-study, FEASIBLE_OFFLINE, 2026-11-05)
- Report: [SER_FEASIBILITY_20261105](../SER_FEASIBILITY_20261105.md). No game, queue or Git commands.
- RTX 5090 driver 621.14 advertises NV rev 1 and EXT rev 2 SER; EXT feature true, reordering hint REORDER.
- No SER in compute: both routes require raygen. EXT query-to-hit-object conversion does not remove that limit.
- SDK DXC 1.9.0.5399 compiles/validates explicit NV and EXT hit-object/reorder SPIR-V intrinsics.
  Native HLSL 6.9 calls are unimplemented; external DXC 1.7 rejects both SER capabilities. Plain raygen controls pass.
- Receipts: `.scratch/ser-study/probe.json`, sources/disassembly and vulkaninfo text; no GPU shader execution.
- Smallest renderer experiment keeps ray queries/manual material evaluation in raygen, adds SBT, RT bindings/barriers,
  host feature/dispatch support and likely one ABI bump. Full closest-hit material conversion can wait.
- NO-GO for immediate shipping; conditional GO for 1-2 focused weeks offline. Planning hypothesis 0-10% trace saving,
  with regressions possible; historical hidden-shading analogy gives only 0.15-0.68 ms gross before overhead/roof savings.
- No new measured GPU cost or quality pass. Request delta 0, ABI unchanged. Live checks require a separate authorized batch.
