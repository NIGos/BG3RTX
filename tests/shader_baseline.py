"""The default baseline of the offline shader gates (key-off SPIR-V byte identity).

AGENTS.md: with a new key off, the ordinary shaders must compile to byte-identical SPIR-V.
A gate proves that against the commit the work starts from: the merge-base with main
(HEAD itself on main, so uncommitted edits are compared with the last commit). A fixed
old commit stops meaning anything once later work changes the ordinary path.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def default_baseline():
    git = lambda *a: subprocess.run(['git', *a], cwd=ROOT, capture_output=True, text=True)
    base = git('merge-base', 'HEAD', 'main')
    if base.returncode == 0 and base.stdout.strip():
        return base.stdout.strip()
    return git('rev-parse', 'HEAD').stdout.strip()
