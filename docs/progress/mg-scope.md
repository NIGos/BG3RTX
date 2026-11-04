## Merge gates sized by changed paths (branch mg-scope, kept offline, 2026-11-04)

- merge-green classifies `git diff --name-only main...ref` before merging. Unknown paths,
  empty diffs and classifier failures keep the full gate. Renames include the deleted source.
- Docs: no build; no repository-wide Markdown/link checker currently exists. Tooling: changed
  Python/PowerShell/JSON syntax, changed job/plan lint with frozen evidence, shared queue and
  camera tests, plus affected registered offline Python CTests. No DXC, C++ build or QA.
- Shaders or common/shader_variant.h: C++ build, shader consumer CTests (including transitive
  local C++ includes), existing cached shader compilation and descriptor interface checks.
  Native/common/build or unclassified paths keep all CTests, shader compile and QA detectors.
- Game, USER_PLAYING, merge ownership/recovery and running-job guards remain in place.
  Test-tree checkout still updates; docs/tooling skip its redundant build. MERGED/NOT MERGED stay.
- Offline: tooling suite, affected Python CTests and PowerShell parsing passed in 39.5 s.
  Classifier/selection regressions cover mixed paths, renames and fail-closed gate errors.
- Planning estimates, not measured merge timings: docs < 0.2 min; tooling 0.2-2 min;
  shaders 3-10+ min; full 10+ min. Cold DXC/cache misses can exceed these estimates.
- No game launch, merge, commit or Git metadata write. Shader request delta: 0. ABI unchanged.
  Check the printed scope/reason and gate list in the next authorized merge-green run.
