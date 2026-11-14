## claude/queue-corpus

- Bug: `scripts/test-queue.ps1` pruned every log older than 3 days at start. After 3 idle days the preflight
  saw `VALID corpus: 0 logs` and refused every job (`corpus: no readable VALID logs`), 2026-11-14.
- Fix 1: `tools/live_batches/keep_logs.py` (called by the runner) keeps the newest 3 VALID logs per save
  (same set as `live_flow.corpus()`) plus siblings sharing the log name or stem; the rest older than 3 days goes.
- Fix 2: `live_flow.py` with an empty live corpus prints `live corpus empty: using the frozen snapshot` and lints
  with `lint_corpus_snapshot.json` (as `--frozen`). A job failing the frozen lint is still refused. No runner flag.
- Tests: `QueueCorpusFallbackTests` in `tests/live_flow_test.py` (fallback equals `--frozen` result; keep-list).
- Preflight with empty corpus now exit 0 for all four opt_profile jobs and hf2-s8-hair-final.
- No live run; not merged.
