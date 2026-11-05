## Exact GI ordering and auxiliary AO (branch gi-perm, tooling integration pending, 2026-11-05)

- Developer raw keys `gi_lane_permute=0` / `gi_ao_r16f=0`, require `dev=1`.
  Vulkan spec IDs 1032/1033; shader request delta 0; no ABI change.
- Morton permutation stays inside each 8x4 wave tile of the original 8x8 group.
  Pixel coordinates, seeds, serial rays/additions and output addresses stay.
  Integer wave reductions elect the original first logical active pixel's bin;
  far-GI group bins/barriers retain their semantics. Below 32 lanes uses identity.
- AO writer and all ordinary readers (both local-shadow paths + frozen LS ref)
  use R16F binding 51. Primary/local PAN diagnostics retain RGBA16F binding 44;
  diagnostic AO folds mirror RGBA. The scalar target starts 1x1 OFF, warms/clears
  full size ON and stays resident for history-preserving A/B. Ordinary writes
  save 6 B/pixel. RGBA remains allocated: this saves bandwidth, not resident memory.
- CPU fixtures PASS: tile bijection/edges/subgroup membership, divergent counter
  bins, all 1024 native AO encodings, diagnostic contracts, allocation failure,
  warmed ON/OFF reuse and resize. Final `scripts/build.ps1 -Test`: 192/192 PASS.
- Full release equivalence vs main, freezing 1032/1033 OFF: 140 identical + 75
  equivalent requests, zero differences, 5075 assignments, 75 universal certificates;
  mandatory negative control PASS. Cache compiled only changed preprocessed requests.
- Final S8/camp/inn replay ON/OFF: 100% byte-equal giRaw, giRawDepth and complete
  rtstats.bin (2848/3163/2873 nonzero counter bins). Same captured data and specs.
- 11-dispatch GPU medians OFF -> ON: S8 13.938272 -> 8.852736 ms;
  camp 60.168640 -> 49.110304 ms; inn 10.618496 -> 10.854592 ms. Noisy, counters
  enabled; not a live saving. Packs omit unsupported resources/AS cases.
- Driver compile-only Ultra hit-sun-emission/native registers main/OFF/ON:
  255/255/255, subgroup 32. Main/OFF executable size 781696 B; ON 916608 B.
- `tools/live_batches/opt_r6/perm/`: inn + S8 Ultra; both keys together,
  OFF/ON/OFF/ON 600 frames, dev=1, fresh QUIET, GI trace decision, no staging.
- Stock frozen/current lint: only missing readiness contracts (10 findings/job).
  Both lint modes PASS with the exact two records injected in an integration fixture.
  Owner/coordinator applies `readiness-integration.patch`; protected tooling untouched.
  Batch status READY_FOR_TOOLING_INTEGRATION, then READY_FOR_LIVE after stock lint.
- In-game GPU cost remains unmeasured. Check inn and Water Queens House Ultra with
  the prepared serial batch after explicit live steering. Features stay off.
- Small hashes/receipts: batch `offline.json`; full logs in `.scratch/gi-perm/`.
  No Git writes, game launch or queue writes; coordinator commits.
