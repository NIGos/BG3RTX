## Water capture tooling (branch water-capture-fix, offline verified, 2026-11-03)
- Energy: dump slot 0 after the core water hook; pair explicit post_core events with the same pass/image.
- Coverage: use published 32/40/48/96/104/112 B strides, pack word 7's marker, decode exact R8_UINT; transport stays strict.
- Fixtures cover missing/wrong-image readbacks, native-water cancellation, nonfinite counts, every stride and truncated R8 data.
- Re-analysis: all 32 wo1 + 4 wf9 energy traces still lack post-water data; all four evening 99100-99103 bursts lack coverage and remain INVALID.
- Validation: build + 144/144 CTests passed (118 s); QA detectors passed; offline report: build/water-capture-fix-reanalysis.json.
- No game launch, commit, ABI/settings/shader change; variant request delta 0. Restart needed for host fix; live qualification remains paused.
