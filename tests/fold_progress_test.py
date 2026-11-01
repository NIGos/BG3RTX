"""Progress fragments (scripts/fold_progress.py): fold docs/progress/*.md into PROGRESS.

    py -3 tests/fold_progress_test.py
"""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import fold_progress as fp  # noqa: E402

FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name} {info}")
    if not ok:
        FAILS.append(name)


PROGRESS = """# BG3RTX progress - 2026-11-01 (main f31c62b, ABI 25)

## User decision needed: promote rf_hit_shade?

- question text

## Older entry (2026-10-31)

- old
"""


def main():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "docs" / "progress").mkdir(parents=True)
        (root / "docs" / "PROGRESS_CODEX.md").write_text(PROGRESS, encoding="utf-8")
        frag = root / "docs" / "progress"
        (frag / "README.md").write_text("# not a fragment\n", encoding="utf-8")
        (frag / "a-branch.md").write_text("## A change (branch a, offline, 2026-11-01)\n\n- a\n", encoding="utf-8")
        (frag / "b-branch.md").write_text("## B change (branch b, READY_FOR_LIVE, 2026-11-02)\n\n- b\n", encoding="utf-8")
        check("check mode passes valid fragments (README skipped)", fp.main(["--root", d, "--check"]) == 0)
        (frag / "c-bad.md").write_text("no heading\n<<<<<<< HEAD\n", encoding="utf-8")
        check("check mode fails a fragment without heading / with conflict markers", fp.main(["--root", d, "--check"]) == 1)
        check("fold refuses while a fragment is invalid", fp.main(["--root", d]) == 1 and (frag / "a-branch.md").exists())
        (frag / "c-bad.md").unlink()
        (frag / "d-long.md").write_text("## Long (2026-11-01)\n" + "- x\n" * fp.MAX_LINES, encoding="utf-8")
        check("a fragment over the line cap fails", fp.main(["--root", d, "--check"]) == 1)
        (frag / "d-long.md").unlink()
        check("fold succeeds", fp.main(["--root", d, "--main", "abc1234"]) == 0)
        text = (root / "docs" / "PROGRESS_CODEX.md").read_text(encoding="utf-8")
        lines = text.splitlines()
        check("title line gets the new main id", lines[0] == "# BG3RTX progress - 2026-11-01 (main abc1234, ABI 25)")
        heads = [l for l in lines if l.startswith("## ")]
        check("decision section stays first, newest fragment next, then older ones",
              heads == ["## User decision needed: promote rf_hit_shade?", "## B change (branch b, READY_FOR_LIVE, 2026-11-02)",
                        "## A change (branch a, offline, 2026-11-01)", "## Older entry (2026-10-31)"], str(heads))
        check("folded fragments deleted, README kept",
              sorted(p.name for p in frag.iterdir()) == ["README.md"])
        check("no fragments: nothing to do", fp.main(["--root", d]) == 0 and
              (root / "docs" / "PROGRESS_CODEX.md").read_text(encoding="utf-8") == text)
    plain = fp.fold("# T (main 1234567)\n\n## Old\n- o\n", [(Path("x.md"), "## New (2026-11-01)\n- n\n")])
    check("without a decision section fragments go above the first entry",
          plain.splitlines()[2] == "## New (2026-11-01)", repr(plain))
    errs = [e for p in fp.fragments(ROOT / "docs" / "progress") for e in fp.problems(p, p.read_text(encoding="utf-8"))]
    check("the repository's own fragments are valid", not errs, str(errs))
    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
