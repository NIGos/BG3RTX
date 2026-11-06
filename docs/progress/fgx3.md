## FG x3 session investigation (branch fgx3, offline verified, 2026-11-06)

- Full evidence and exact log/code pointers: [analysis](../evidence/fgx3-analysis.md).
- Proven: 11 of 12 SDK eOn interpolation shutdowns explicitly follow focus-loss
  notices. One short shutdown is unexplained; SyncInterval flips do not prove causation.
- Rendered rates in sampled windows: x3 25.25 / 24.16 fps, x2 28.52 / 33.17 fps.
  These are moving, unequal windows, not a controlled x3 cost comparison.
- Proven code defects: periodic GetState queried twice, draining its presented count;
  failed tag/constants publication could leave FG enabled. Use one sample per present
  and fail closed on publication errors. No historical publication error is proved.
- New release-visible, rate-limited FG cadence summary reports SDK presented/rendered,
  proxy ratio, SDK pause/focus notices, API publication results and destroy-wait ms.
  Preserve required fences; bound successful destruction logging and name image/roles/extent.
- 1,609 successful destroy waits do not measure their duration; x3 serialization
  remains plausible. NR private slots are not the tagged colour inputs. NR skip
  messages do not consistently precede focus pauses. Vulkan FG VSync is unsupported
  by the SDK; a causal VSync hitch is unproved. No speculative override added.
- fps_cap is correctly a rendered ceiling: 58 -> 17,241 us, removed at 15:07:10.
  No multiplier division, new INI keys, ABI change or shader request delta (0).
- Validation: scripts/build.ps1 -Test PASS 204/204 (build 56 s, CTest 194.13 s).
  CPU publication/count tests and QA detectors pass; git diff --check passes.
- One later authorized live check must hold focus through x2/x3/x2 windows in one
  run, with NR and the real Ultra preset. Require successful tags/constants, stable
  SDK multiplier, no unexplained SDK pauses, and compare destroy-wait total/max ms.
  Actual display cadence needs display-change timing; present calls alone cannot prove it.
- No game launch, queue access or push. No stutter fix or runtime cost claim yet.
- Commit blocked: sandbox denies .git/worktrees/fgx3/index.lock outside writable roots.

