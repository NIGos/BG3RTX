## Cold runtime cache readiness (branch cold-cache, kept offline, 2026-11-04)
- Chose (b): runtime COM DXC identity/debug keys and store differ from offline CLI cache; renew only pending prefetch inactivity on new completed builds, retain 15-minute launch cap. Requests delta 0.
- wf9 replay: 27 warmup completions; inactivity 11.945 s at old 180 s timeout. Camera checks 661, live-flow 61 and batch lint 6 pass; no game launch or commit.
