## RR submission ownership (branch layer-submit, offline PASS, 2026-11-05)

- L11 confirmed: `layer/rrpost.cpp:458-472` previously wrapped 256 descriptor slots
  without recording/completion ownership. Keep exact recording leases; reuse only
  after reset/free and existing queue-specific lifetime completion. Exhaustion fails closed.
- L12 confirmed: `layer/ngx.cpp:829-841,950,1262-1265` published guide init/freshness
  during recording. Reuse `RecordingInit`; publish only at successful submit,
  cancel pending state on discard and refuse unrelated unsubmitted producers.
- L17 confirmed: `layer/rrpost.cpp:1152-1154,1193-1194,1355-1359` now keeps the
  temporal output local and publishes history validity only at successful submit.
- Both submit APIs reuse the L15 success hooks (`layer/layer.cpp:515,579`). Exact
  secondary IDs follow the existing primary execute bookkeeping. Failed begin
  preserves previous leases; reset/free/pool discard cancels pending state.
- PASS: `python tests/recording_lifecycle_test.py` (7 tests); `git diff --check`.
  Extended `fg_copy_lease_test` with ring saturation, executable resubmission,
  pending GPU ownership, stale IDs and discard/submit publication cases.
- BelowNormal `scripts/build.ps1 -Test` PASS: build 74 s; CTest 184/184 in
  255.62 s, including the new C++ ownership cases. No git writes, game launch,
  queue writes, shared SDK cleanup or changes to other lanes' owned files.
- No new key, ABI or shader change; variant request delta 0. GPU cost unmeasured.
  Coordinator owns the commit/merge. A later authorized Ultra smoke should
  check RR remains active in normal submit order.
