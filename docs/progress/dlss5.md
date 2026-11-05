## Native DLSS 5 phase 2b (branch dlss5, READY_FOR_LIVE candidate, 2026-11-05)

- Added layer/dlss5.cpp/.h and a fixed codec/guide shader. Same command buffer:
  successful pre-post RR -> encode/guides -> NGX feature 18 -> decode g_z -> rrpost
  temporal/final -> FG. Linear pre-exposed HDR; no D3D12, split or host ABI change.
- Off in every preset. Experimental page dlss5_nr; RR/pre-post required, restart
  to enable. Browse field dlss5_nr_runtime in both UIs; live dlss5_nr_bypass for A/B.
- Original NGX owner preserved with FG 2+ and FG Off. Staged bin/sl path prepended
  to forwarded Init; feature created lazily, capability checked, resized/reset as
  needed. Missing inputs/debug/unsupported recording skip; failure disables session.
- Eight immutable recording slots retire using actual submitted queue completion.
  CPU tests include boundary, guide scale/subrect/jitter/reset and layout commit
  versus discarded/failed submissions, plus EXT/core BDA-chain policy. GPU reports
  NR+codec with separate intervals.
- Launcher: game-bin then user path, signed preferred; version/SHA-256/offline trust
  logged and UI warnings shown. Only changed bytes copied/verified into bin/sl.
  No real NR binary was staged in this work. NR is rejected by package guards/tests.
- NGX core pinned; armed Shutdown calls are explicit no-ops (known fixture stall).
  Device drain releases NR; abrupt exit performs no late GPU destructor work.
  Device recreation disables NR until restart. No on-disk NVIDIA changes or spoof.
- Offline: final BelowNormal build.ps1 -Test passed 189/189 (265.66 s CTest,
  11 s build): .scratch/dlss5-phase2b-last.log. No test failures remain.
- Shader manifest delta 0; one bare shader / three fixed entries compile + spirv-val.
  All 217 existing requests byte-identical to f8942a23 by equal inputs.
- Staged job: tools/live_batches_staged/dlss5/jobs/dlss5-s8-ultra-native.json.
  S8 Water Queens House, real Ultra/FG 2, OFF/ON/OFF/ON: 600 measured + 30 settling
  frames each, then PNG pair. Per-window NR cost/FG 2.00, no NR skip/failure/device
  loss; structural and synthetic gates pass. Not queued/run; fresh authorization needed.
- Prior standalone signed 310.8.0: NR median/p90 6.520/7.147 ms at 3840 x 1600;
  codec total 6.785/7.379 ms. In-game cost and visible quality remain unmeasured.
  Custom 310.8.3 is app-accepted but core-rejected (0xbad0000b). Internal NR VUID
  09600 remains unresolved; no validation suppression or quality pass is claimed.
- No game launch, queue write, git write, preset promotion or common/host_api.h change.
