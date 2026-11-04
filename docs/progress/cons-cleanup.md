## Rejected option cleanup (branch cons-cleanup, tests PASS, raw identity FAIL, 2026-11-05)

- 1 removed: compact hit state, alpha metadata cache and empty water tiles; keys/help, specs,
  implementations, dedicated tests and jobs. Initial code/test step: 370 lines removed; request delta 0.
- 2 removed: water off-screen hit shading and cost bits 1/2/4/7, hit-only layouts and world-light prep.
  Deleted subsets: water core 275 lines (includes step 1), water shaders 104, compose helper 62. Request delta -10.
- 3 removed: alpha spill, counters, key and budget helper/test. Deleted subsets: capture 26 lines,
  budget helper 10 and tests 22. Request delta 0; ordinary overflow fallback retained.
- 4 removed: Pak publication, readback and hooks, keys and dedicated jobs. Deleted subsets:
  readback implementation 173 lines and production fixture 210. Request delta 0.
  Native Tilesets remain hidden/off; requested(native) retains their independent capability.
  Offline Pak tools/decoders remain because they still have consumers.
- 5 removed: generated rows/producers and IP grass; audited static/wind paths remain.
  Deleted subsets: generated header 38 lines, producer 370, generated-only tests 201. Request delta -19.
  Retired family/counter slots remain reserved, preserving ordinary data offsets.
- 6 removed: Half trace and resolve; Full behavior retained. Dedicated resolve shader: 43 lines removed.
  Request delta -14: 12 remaining half requests plus 2 resolve requests; step 5 already removed 4 half requests.
- 7 removed: current-sun capture/clone/private submit, layouts, dispatch, variants and jobs.
  Host diff: 1,220 lines removed / 16 added; dedicated source deletions total 764 lines (overlapping subset).
  Ordinary sun/cascade/OMM remain; shared ownership helpers restored verbatim after the first link check.
  ABI 26 unchanged; retired callback null/reserved. Request delta -2.
- 8 removed: GI motion-sample and previous-radiance guards; SpecIds 1001/1002 reserved in a comment.
  Measured source subset: 186 lines removed / 19 added, including the 58-line guard header. Request delta 0.
- 9 kept under the conditional-stop rule: sharc_device supplies shaderInt64 for shipped water and
  retained hair through shared device feature/fallback setup. Clean removal needs separating that
  capability first (layer/rtscene.cpp, layer/water.cpp, layer/hair_primary.cpp). One-ray files preserved.
- Each logical removal 1-8 built successfully; 1/2 used core builds, 3-8 full builds.
- Manifest: 167 -> 122 requests, delta -45. Counts above are measured subsets, not additive totals.
- Evidence and exact removal markers retained at the TRACK outcomes; no game launch or commit.
- Jobs: 97 survive; mixed water fixtures retain smoke/wf9/post. Snapshot readiness rows 330 -> 184,
  regex rows 391 -> 371; removed-key projection preserves existing measurements, without invented timings.
- Required build.ps1 -Test: PASS, all 152 CTests (164 seconds), including settings/schema and launcher UI.
- Defaults generator/check PASS. Unknown old INI keys load silently and have no remaining consumers.
- Strict identity FAIL vs bundle35 1a7e461c5b03: 79/214 remaining requests identical, 135 different; no compile errors.
  The single CLI run covered 215 requests (80 identical); the unreferenced 52-line receiver shader was then removed.
  All 214 remaining requests are covered by that receipt; .scratch/cons-cleanup-identity-remaining.json records the subset.
- Retained Fresnel default-equivalence PASS after symmetric unused-capability trimming; this is not raw byte identity.
- Tracked source diff: 4,009 lines removed / 230 added. No new repository file exceeds 20 MB. ABI 26 unchanged.
- Extra standalone queue-child permission check is incomplete: pwsh is absent; synthetic PS5 child transport passed.
