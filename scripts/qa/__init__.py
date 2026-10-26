"""Automated image-quality QA for BG3RTX (docs/QA.md).

    python -m scripts.qa run --scenes S1,S3 [--full-res] [--reference] [--temporal 3]
    python -m scripts.qa analyze <captures/qa/<stamp>/<scene>>   (re-run the analysis only)

One game run per scene: consecutive traces of the same static view with the
effects toggled live between them (scripts/live_phases.py), then per-effect
contribution maps and anomaly detectors, crops of the worst regions and a
report under captures/qa/<stamp>/. Raw captures are pruned afterwards.
"""
