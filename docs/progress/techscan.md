## Exact GPU technique scan (branch techscan, analysis only, 2026-11-05)

- Added `docs/PERF_TECHNIQUES_20261105.md`: source/log evidence, exactness obstacles, net estimates, effort/risk and cheapest offline proofs.
- Ranked: lane/tile permutation 0–0.40 ms; auxiliary AO R16F 0–0.06; hidden-hit wavefront 0–1.50; reduced GI TLAS 0–0.70; async AS 0–0.40.
- All ranges are unmeasured planning estimates including zero; regression is possible. No achieved saving or quality pass claimed.
- Reduced TLAS needs bounds around visible origins, current submit-time transforms, original InstanceIndex mapping and cap/tie equivalence.
- Wavefront must preserve individual emission/shading addition order, fp32 operands, source-pixel seeds and full snapshot lifetimes.
- Most intermediates already use 16F or narrower. No active full-resolution RGBA32F GI image to halve in stock Ultra.
- Origin depth reuse cannot reproduce triangle normals/offsets; origin/sun fusion would retain the expensive primary traversal.
- No completed/rejected technique proposed again. Shader request delta 0; no implementation or ABI change.
- No build, compilation, replay, game, queue, Git mutation or live batch. Only the two requested documentation files written.
