"""The coverage matrix's HANDLING table (scripts/qa/coverage.py) stays consistent with main.

    py -3 tests/coverage_handling_test.py

Every cell uses the legend; every "key" cell names its keys in HANDLING_KEYS; every key named
there exists in bg3rtx.ini and is off by default (a key that became default-on must turn its
"key" cells into "yes"); the table is dated; the markdown renders the handling columns and notes.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.qa import coverage as cov  # noqa: E402

FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name} {info}")
    if not ok:
        FAILS.append(name)


def main():
    legend = {cov.Y, cov.N, cov.NA, cov.PART, cov.KEY}
    bad = [f for f, row in cov.HANDLING.items() if len(row) != 6 or not set(row) <= legend]
    check("every HANDLING row has 6 legend cells", not bad, str(bad))
    keyed = [f for f, row in cov.HANDLING.items() if cov.KEY in row]
    check("every 'key' row names its keys", all(f in cov.HANDLING_KEYS for f in keyed),
          str([f for f in keyed if f not in cov.HANDLING_KEYS]))
    check("HANDLING_KEYS only names families of the table", set(cov.HANDLING_KEYS) <= set(cov.HANDLING))
    check("the table is dated", bool(re.match(r"\d{4}-\d\d-\d\d", cov.HANDLING_DATE)), cov.HANDLING_DATE)
    ini = {}
    for line in (ROOT / "bg3rtx.ini").read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"\s*([a-z][a-z0-9_]*)\s*=\s*(\S*)", line)
        if m:
            ini[m.group(1)] = m.group(2)
    named = sorted({k for v in cov.HANDLING_KEYS.values() for k in re.findall(r"\b(?:rt|gi|rf)_[a-z0-9_]+\b", v)})
    check(f"{len(named)} named keys exist in bg3rtx.ini", named and all(k in ini for k in named),
          str([k for k in named if k not in ini]))
    check("named keys are off by default (else the cell is 'yes', not 'key')",
          all(ini.get(k) == "0" for k in named), str({k: ini.get(k) for k in named if ini.get(k) != "0"}))
    fam = "forward: water"
    rec = {"capture": str(ROOT / "captures" / "20261101-000000"), "scene": "S8", "measured": True, "has_dumps": True,
           "passes": {}, "families": {fam: {"draws": 2, "share": 0.05}}}
    md = cov.to_markdown([rec])
    check("markdown shows the water row with its handling cells",
          "| forward: water | no | key | no | no | key | n/a |" in md)
    check("markdown lists the keys and the date", "rt_water_shadows" in md and cov.HANDLING_DATE in md)
    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
