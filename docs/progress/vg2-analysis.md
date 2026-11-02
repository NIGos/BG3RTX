## vg2 VRAM guard analysis (2026-11-02, branch codex-vg2-analysis)
- vg2 21:52 (S8, simulated 15640 MB budget): static windows fine (ours 4.2 GB, process 14.8 GB, energy +0.60% within 0.92%); turns over budget: BLAS reserve 448 -> 4178 MB and RT capture refusals raised opaque drops. Fixed in codex-vram-motion; vg3 (real budget) passed.
- Full table: docs/analysis/VG2_ANALYSIS_20261102.md.
