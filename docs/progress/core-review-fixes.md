## Core review guards (branch core-review-fixes, offline verified, 2026-11-03)
- Borrowed water lighting has no lifetime provider: refuse F-1 hit shading; retain ordinary compose and same-frame shading. Key-off unchanged.
- Reflection async fallback now requires the installed full/half mode to match requested bindings, dispatch and resolve, including SHaRC/hit variants.
- Full build and 144/144 CTests passed; strengthened lifetime and bidirectional resolution regression tests rebuilt and passed 2/2.
- No shaders or shader includes changed; identity has no touched requests, manifest delta 0, ABI unchanged. No game launch or GPU cost measurement.
