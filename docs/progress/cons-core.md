## Core consolidation (branch cons-core, offline verified, 2026-11-04)

- C1 fixed: sun DSL/pool/sets built locally, cleaned on failure, published together; retries never see a partial ring.
- C2 fixed: emissive layout replacement builds all three layouts locally; failure cleans candidates and preserves old state.
- C3 fixed: removed the unused probe pipeline and per-frame shader request; census callbacks remain.
- C5 fixed: make_layout publishes its DSL/pipeline-layout pair only after success; failed candidates are destroyed.
- C6 fixed: reflection layout replacement preserves old state on failure and retires every cached RF/SHaRC variant once.
- C9 fixed: water pass-end read g_csun/g_cls/g_crr before g_mu, while lighting writes them under g_mu;
  the read now shares that lock (host callbacks may overlap under shared hot-reload locks).
- C11 fixed: both reflection and compose require want_hit before hit layouts/pipeline requests;
  key off also skips material-use registration and world-light allocation/preparation.
- C4 not confirmed: sun ring has 32 sets, minimum reuse age 8 presents, skips when full;
  no demonstrated pending GPU consumer at reuse. Age is not a completion guarantee; no safety claim added.
- C7 not confirmed: light/trust/water rings have 8 slots; no demonstrated wrap with an unfinished consumer.
  Core ABI 26 exposes no completion token; changing retirement needs a coordinated host contract.
- C8 not confirmed: image-init/parity do advance on recording, but no reproduced discarded GI producer;
  core has no submit/discard notification. Do not guess a repair from frame age or change temporal behavior.
- C12 not confirmed: mapped counter reads use an 8-slot ring, not a covering fence;
  no demonstrated unfinished write at consumption. Same host completion-contract limitation as C7.
- C10 deferred to cleanup/DXC identity stream as assigned; shaders and variants untouched (request delta 0).
- D3 fixed in owned files: removed probe-template claims, obsolete water defaults/uncomposed wording,
  and RR one-ray comment (Ultra uses two). settings.h wf9 blocker comment belongs to cons-product.
- D8: fresh full /W3 build compiled all 303 targets/actions, including layer/core/common/launcher.
  Core warnings 0; common C4996 sites 2 -> 0 (_fsopen with original sharing, bounded sscanf_s;
  non-MSVC paths preserved). Live-log sharing is covered by the default-writer test.
  Remaining C4996: layer/alphatest.cpp:425,478 (sscanf); tools/xfb_patch_test.cpp:52 (getenv);
  tests/log_rotate_test.cpp:10,12,27,28 and tests/assetdb_test.cpp:56,197,224 (fopen). Launcher warnings 0.
- Tests added: core_resource_init_test (213 checks against extracted production functions with fake Vulkan,
  allocation failure/retry, layout replacement failure, 9 cached variants retired once), plus 3 source guards.
  Existing log_rotate/vt_alpha_masks tests extended for failed/shared default open and maximum-length filename.
- Validation: all 152 CTests passed in 141.37 s; after the final log-sharing adjustment, rebuild + 3 affected
  CTests passed again. Initial shared-cache access failed outside the sandbox; local cache snapshot retry passed.
- VS bootstrap prints "vswhere.exe not recognized"; the outer shell reported 1 despite the full test pass.
  Focused invocation explicitly returned build.ps1's LASTEXITCODE 0. Bootstrap scripts were not changed.
- Logs: build/cons-core-clean-build.log, build/cons-core-final.log, build/cons-core-focused.log.
  No game launched or GPU cost claimed; rendering shaders/presets and ABI unchanged. No commits made.
