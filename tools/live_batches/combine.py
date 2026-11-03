"""Combine disjoint phase groups into one launch plan per save (pure/offline).

The caller creates one queue job referencing the returned plan. Never concatenate
already running jobs: startup-latched settings and validation mode must match.
"""
from copy import deepcopy


def combine(groups):
    plan = [deepcopy(step) for group in groups for step in group]
    labels = [p["label"] for p in plan]
    if not plan or len(labels) != len(set(labels)):
        raise ValueError("combined plan needs unique labels")
    if any(p.get("turn_mode") not in (None, "keys") for p in plan):
        raise ValueError("camera turns must use held Q/E")
    return plan
