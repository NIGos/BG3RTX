"""Liveness: does every feature that is on actually do work? (docs/REGRESSION_PROCESS.md)

The A/B gates judge deltas and the runner's "expect" patterns only check that a line exists, so a feature can be on
and do nothing for weeks while every gate passes ("OMM accelerated commits 0" printed every second; "level unknown:
nothing trusted" in every log). This checker reads any game log, resolves the settings of each `[cfg] settings read`
segment (preset from that line, then the `[cfg] knobs` and the (truncated) `[cfg] keys` lines, the rest from
bg3rtx.ini + data/release.ini and data/settings_schema.json), and judges each row of tools/live_batches/liveness.json
per preset group of the log:

  ALIVE     the observable proves work in this preset
  DEAD      the row is on, the observable says no work (all values 0, a forbidden line, or a line that never came)
  UNPROVEN  the row is on but the log cannot tell (too short, no line, the scene has no water)

    py -3 tools/live_batches/liveness.py run.log [more.log ...] [--gate] [--json out.json]
    py -3 tools/live_batches/liveness.py captures/queue/logs            (every *.log, cached, with progress)
    py -3 tools/live_batches/liveness.py --rules                        (AGENTS.md rule: every on key has a row)

--gate exits 1 when any row is DEAD (the release smoke and jobs with "liveness": "gate"); otherwise exit 0.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SPEC = Path(__file__).with_name("liveness.json")
VERSION = 3   # 2: 'follows'; 3: a 'once' row as a 'follows' cause
ALIVE, DEAD, UNPROVEN = "ALIVE", "DEAD", "UNPROVEN"
_RANK = {ALIVE: 0, UNPROVEN: 1, DEAD: 2}
_TS = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3})")
_READ = "[cfg] settings read"
_PRESET = re.compile(r"preset=(\w+)")
KEYS_LINE_LIMIT = 2000   # layer/util.cpp log() cuts a line near 2 KB: the last token of a longer line is partial


def read_ini(path):
    out = {}
    try:
        text = Path(path).read_text(encoding="utf-8-sig")
    except OSError:
        return out
    for line in text.splitlines():
        line = line.split("#", 1)[0].split(";", 1)[0].strip()
        if "=" in line and not line.startswith("["):
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


class Profiles:
    """Player settings: bg3rtx.ini + data/release.ini, then a preset's knob choices and extras (settings schema)."""

    def __init__(self, root=ROOT):
        self.base = read_ini(Path(root) / "bg3rtx.ini")
        self.base.update(read_ini(Path(root) / "data/release.ini"))
        schema = json.loads((Path(root) / "data/settings_schema.json").read_text(encoding="utf-8"))
        self.knobs = {k["id"]: k for k in schema["knobs"]}
        self.extra = {p["id"]: p.get("extra", {}) for p in schema["presets"]}
        self._cache = {}

    def knob_keys(self, knob_id, value):
        """Keys of one knob value: a choice id or index, or a slider number."""
        knob = self.knobs.get(knob_id)
        if not knob:
            return {}
        if knob["kind"] == "choice":
            for i, choice in enumerate(knob["choices"]):
                if value in (choice["id"], i, str(i)):
                    return dict(choice["keys"])
            return {}
        try:
            number = float(value) * float(knob.get("scale", 1))
        except (TypeError, ValueError):
            return {}
        return {key: format(number, ".9g") for key in knob["keys"]}

    def preset(self, name):
        if name not in self._cache:
            state = dict(self.base)
            if name in self.extra:
                for knob_id, knob in self.knobs.items():
                    value = knob.get("preset_values", {}).get(name)
                    if value is not None:
                        state.update(self.knob_keys(knob_id, value))
                state.update(self.extra[name])
            self._cache[name] = state
        return dict(self._cache[name])


def _tokens(text, limit=KEYS_LINE_LIMIT):
    parts = text.split()
    if len(text) >= limit and parts:
        parts = parts[:-1]   # cut by the log buffer
    return [p.split("=", 1) for p in parts if "=" in p]


def segment_settings(lines, profiles):
    """The resolved settings after a `[cfg] settings read` line and its knobs/keys lines."""
    first = lines[0]
    presets = _PRESET.findall(first)
    preset = presets[-1] if presets else None
    state = profiles.preset(preset) if preset else dict(profiles.base)
    for line in lines[1:]:
        i = line.find("[cfg] knobs: ")
        if i >= 0:
            for knob, value in _tokens(line[i + 13:]):
                if value != "custom":
                    state.update(profiles.knob_keys(knob, value))
            continue
        i = line.find("[cfg] keys:")
        if i >= 0:
            for key, value in _tokens(line[i + 11:]):
                if value != "(default)":
                    state[key] = value
    return preset or "none", state


def _number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def condition_holds(state, when):
    for key, want in when.items():
        have = state.get(key)
        if want.startswith(">") or want.startswith("!"):
            n = _number(have)
            if n is None or (want.startswith(">") and not n > float(want[1:])) or (want.startswith("!") and n == float(want[1:])):
                return False
        else:
            a, b = _number(have), _number(want)
            if (a != b) if (a is not None and b is not None) else (have != want):
                return False
    return True


def _needle(pattern):
    """Longest literal run of a regex, for a fast substring pre-filter (always a substring of every match)."""
    runs, cur, i, depth, alt_at = [], "", 0, 0, None
    while i < len(pattern):
        c = pattern[i]
        if c == "\\" and i + 1 < len(pattern):
            n = pattern[i + 1]
            if n.isalnum():          # \d \s \w  ...: not a literal
                runs.append((cur, depth)); cur = ""
            else:
                cur += n
            i += 2
            continue
        if c == "[":                 # a character class: skip it
            runs.append((cur, depth)); cur = ""
            j = i + 1
            if j < len(pattern) and pattern[j] == "]":
                j += 1
            while j < len(pattern) and pattern[j] != "]":
                j += 2 if pattern[j] == "\\" else 1
            i = j + 1
            continue
        if c in "*?{":               # the previous character is optional
            runs.append((cur[:-1], depth)); cur = ""
            if c == "{":
                i = pattern.find("}", i) if pattern.find("}", i) > 0 else len(pattern)
        elif c in "+":
            runs.append((cur, depth)); cur = ""
        elif c == "(":
            runs.append((cur, depth)); cur = ""; depth += 1
        elif c == ")":
            runs.append((cur, depth)); cur = ""; depth -= 1
        elif c == "|":
            runs.append((cur, depth)); cur = ""
            alt_at = depth if alt_at is None else min(alt_at, depth)
        elif c in ".^$":
            runs.append((cur, depth)); cur = ""
        else:
            cur += c
        i += 1
    runs.append((cur, depth))
    if alt_at == 0:
        return ""                    # a top-level alternation: no common literal
    # a run inside a group may be one alternative or optional ("(x)?"): only top-level runs are safe
    return max((r for r, d in runs if d == 0), key=len, default="")


class Rule:
    def __init__(self, row):
        self.row = row
        self.id = row["id"]
        self.kind = row["kind"]
        self.when = row.get("when", {})
        self.line = re.compile(row["line"])
        self.needle = _needle(row["line"])
        self.proof = re.compile(row["proof"]) if row.get("proof") else None
        self.proof_needle = _needle(row["proof"]) if row.get("proof") else ""
        self.only_if = re.compile(row["only_if"]) if row.get("only_if") else None
        self.only_needle = _needle(row["only_if"]) if row.get("only_if") else ""
        self.min = float(row.get("min", 0))
        self.min_lines = int(row.get("min_lines", 3))
        self.min_s = float(row.get("min_s", 120))
        self.dead_after_s = float(row.get("min_s", 0)) if row["kind"] == "positive" else 0.0
        self.once = bool(row.get("once"))
        self.silent_dead = bool(row.get("silent_dead"))
        self.follows = list(row.get("follows", []))


def load_spec(path=SPEC):
    spec = json.loads(Path(path).read_text(encoding="utf-8"))
    return spec, [Rule(r) for r in spec["rows"]]


PRESETS = ("low", "medium", "high", "ultra")


def on_keys(profiles):
    """{key: [presets]} for every key whose player value (bg3rtx.ini + release.ini + preset) is a non-zero number."""
    out = {}
    for preset in PRESETS:
        for key, value in profiles.preset(preset).items():
            n = _number(value)
            if n is not None and n != 0:
                out.setdefault(key, []).append(preset)
    return out


def covered_keys(spec):
    return {k for row in spec["rows"] for k in list(row.get("when", {})) + list(row.get("covers", []))}


def rule_problems(spec, profiles):
    """The AGENTS.md rule: a key that is on in a preset (or by default) needs a liveness row, an exempt reason, or
    (keys on before 2026-11-23 only) a baseline_no_row entry. Also checks the rows against the profiles."""
    problems = []
    on = on_keys(profiles)
    covered, exempt, baseline = covered_keys(spec), set(spec.get("exempt", {})), set(spec.get("baseline_no_row", []))
    for key in sorted(set(on) - covered - exempt - baseline):
        problems.append(f"{key} is non-zero in {','.join(on[key])}: add its liveness row to tools/live_batches/liveness.json "
                        f"(the log line that proves it works) or an 'exempt' reason")
    for key in sorted(baseline & (covered | exempt)):
        problems.append(f"{key} has a row or exemption now: remove it from baseline_no_row")
    for key in sorted(baseline - set(on)):
        problems.append(f"{key} is no longer non-zero in any preset: remove it from baseline_no_row")
    seen = set()
    for row in spec["rows"]:
        rid = row.get("id")
        if rid in seen:
            problems.append(f"duplicate row id {rid}")
        seen.add(rid)
        if row.get("kind") not in ("positive", "appear", "absent"):
            problems.append(f"{rid}: kind must be positive, appear or absent")
            continue
        for field in ("line", "proof", "only_if"):
            if row.get(field):
                try:
                    rx = re.compile(row[field])
                except re.error as e:
                    problems.append(f"{rid}: {field} does not compile: {e}")
                    continue
                if (row["kind"] == "positive" and field == "line" or field == "only_if") and "v" not in rx.groupindex:
                    problems.append(f"{rid}: {field} needs a (?P<v>...) group")
        for cause in row.get("follows", []):
            if cause not in {r.get("id") for r in spec["rows"]} or cause == rid:
                problems.append(f"{rid}: follows unknown row {cause}")
        actual = [p for p in PRESETS if condition_holds(profiles.preset(p), row.get("when", {}))]
        if actual != row.get("presets"):
            problems.append(f"{rid}: presets {row.get('presets')} but 'when' holds in {actual}")
    return problems


def _seconds(line):
    m = _TS.match(line)
    if not m:
        return None
    h, mi, s, ms = (int(x) for x in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


def segments(text, profiles):
    """[(preset, state, start_s, end_s, [lines])], split at every `[cfg] settings read` line."""
    raw = text.splitlines()
    starts = [i for i, l in enumerate(raw) if _READ in l]
    if not starts or starts[0] != 0:
        starts = [0] + starts
    out = []
    offset, last = 0.0, None
    times = []
    for line in raw:
        t = _seconds(line)
        if t is not None:
            if last is not None and t + offset < last - 43200:
                offset += 86400
            last = t + offset
        times.append(last)
    for n, s in enumerate(starts):
        e = starts[n + 1] if n + 1 < len(starts) else len(raw)
        lines = raw[s:e]
        if _READ in lines[0]:
            head = [lines[0]] + [l for l in lines[1:8] if "[cfg] knobs: " in l or "[cfg] keys:" in l]
            preset, state = segment_settings(head, profiles)
        elif len(starts) > 1:
            # the header before the first settings read: launch-time lines go with the first segment's settings
            preset, state = segment_settings([raw[starts[1]]] + [l for l in raw[starts[1] + 1:starts[1] + 8]
                                             if "[cfg] knobs: " in l or "[cfg] keys:" in l], profiles)
        else:
            preset, state = "none", dict(profiles.base)
        t0 = next((times[i] for i in range(s, e) if times[i] is not None), None)
        t1 = times[e - 1] if e else None
        if n + 1 < len(starts):
            t1 = times[starts[n + 1]] if times[starts[n + 1]] is not None else t1
        out.append((preset, state, t0, t1, lines))
    return out


def _value(match):
    try:
        return float(match.group("v"))
    except (IndexError, TypeError, ValueError):
        return None


def judge(rule, segs, detailed=False):
    """{preset: result} for one rule over the segments where its settings hold.

    detailed: the log has `[cfg] detailed logging: 1`, so a "silent_dead" row's stats line must be printed."""
    groups = {}
    for preset, state, t0, t1, lines in segs:
        if not condition_holds(state, rule.when):
            continue
        if rule.once:
            preset = "launch"   # a launch-time line: one judgement for the whole log
        g = groups.setdefault(preset, dict(seconds=0.0, lines=0, values=[], first=None, proof=False, scene=rule.only_if is None))
        if t0 is not None and t1 is not None:
            g["seconds"] += max(0.0, t1 - t0)
        for line in lines:
            if rule.only_if is not None and not g["scene"] and rule.only_needle in line:
                m = rule.only_if.search(line)
                if m and (_value(m) or 0) > 0:
                    g["scene"] = True
            if rule.proof is not None and not g["proof"] and rule.proof_needle in line and rule.proof.search(line):
                g["proof"] = True
            if rule.needle not in line:
                continue
            m = rule.line.search(line)
            if not m:
                continue
            g["lines"] += 1
            if g["first"] is None:
                g["first"] = line[:160]
            if rule.kind == "positive":
                v = _value(m)
                if v is not None:
                    g["values"].append(v)
    out = {}
    for preset, g in groups.items():
        r = dict(seconds=round(g["seconds"], 1), lines=g["lines"])
        if rule.kind == "positive":
            good = [v for v in g["values"] if (v >= rule.min if rule.min > 0 else v > 0)]
            r["max"] = max(g["values"]) if g["values"] else None
            if good:
                status, why = ALIVE, f"{len(good)} of {len(g['values'])} lines show work (max {r['max']:g})"
            elif not g["scene"]:
                status, why = UNPROVEN, "scene precondition not met (only_if)"
            elif not g["values"] and rule.silent_dead and detailed and g["seconds"] >= rule.min_s:
                status, why = DEAD, f"its stats line never printed in {g['seconds']:.0f} s with detailed logging"
            elif len(g["values"]) >= rule.min_lines and g["seconds"] >= rule.dead_after_s:
                status, why = DEAD, f"{len(g['values'])} lines, every value {'< %g' % rule.min if rule.min > 0 else '0'}"
            else:
                status, why = UNPROVEN, f"{len(g['values'])} lines (need {rule.min_lines})"
        elif rule.kind == "appear":
            if g["lines"]:
                status, why = ALIVE, f"{g['lines']} lines"
            elif g["seconds"] >= rule.min_s:
                status, why = DEAD, f"no line in {g['seconds']:.0f} s"
            else:
                status, why = UNPROVEN, f"only {g['seconds']:.0f} s on (need {rule.min_s:.0f})"
        else:   # absent
            if g["lines"]:
                status, why = DEAD, f"{g['lines']} forbidden lines"
            elif rule.proof is None and g["seconds"] >= rule.min_s or g["proof"]:
                status, why = ALIVE, "no forbidden line" + (" (path ran)" if g["proof"] else f" in {g['seconds']:.0f} s")
            else:
                status, why = UNPROVEN, "the path never ran (no proof line)"
        r.update(status=status, why=why)
        if g["first"] and status == DEAD:
            r["example"] = g["first"]
        out[preset] = r
    return out


def check_text(text, rules, profiles):
    segs = segments(text, profiles)
    detailed = "[cfg] detailed logging: 1" in text
    result = {rule.id: judge(rule, segs, detailed) for rule in rules}
    # 'follows': a row whose cause is another row (the sun resolve pass needs the level). A DEAD row whose cause row is
    # DEAD in the same preset says so: fix the cause first.
    for rule in rules:
        for preset, r in result[rule.id].items():
            if r["status"] != DEAD:
                continue
            # a 'once' cause row is judged per launch, not per preset
            causes = [c for c in rule.follows
                      if (result.get(c, {}).get(preset) or result.get(c, {}).get("launch", {})).get("status") == DEAD]
            if causes:
                r["why"] += f" (follows {', '.join(causes)} DEAD)"
                r["follows"] = causes
    return result


def worst(per_preset):
    return max((r["status"] for r in per_preset.values()), key=_RANK.get, default=None)


def log_status(per_preset):
    """One status per log: DEAD if any preset is DEAD, else ALIVE if any is ALIVE, else UNPROVEN (None: never on)."""
    states = {r["status"] for r in per_preset.values()}
    return DEAD if DEAD in states else ALIVE if ALIVE in states else UNPROVEN if states else None


def summary_counts(result):
    counts = {ALIVE: 0, DEAD: 0, UNPROVEN: 0}
    for per_preset in result.values():
        for r in per_preset.values():
            counts[r["status"]] += 1
    return counts


def report_lines(name, result):
    counts = summary_counts(result)
    lines = [f"liveness {name}: ALIVE {counts[ALIVE]}, DEAD {counts[DEAD]}, UNPROVEN {counts[UNPROVEN]} (row x preset)"]
    for row_id, per_preset in result.items():
        for preset, r in sorted(per_preset.items()):
            if r["status"] == DEAD:
                lines.append(f"  DEAD {row_id} [{preset}]: {r['why']}" + (f" | {r['example']}" if r.get("example") else ""))
    for row_id, per_preset in result.items():
        for preset, r in sorted(per_preset.items()):
            if r["status"] == UNPROVEN:
                lines.append(f"  UNPROVEN {row_id} [{preset}]: {r['why']}")
    return lines


def _spec_hash(path):
    return hashlib.sha256(Path(path).read_bytes() + str(VERSION).encode()).hexdigest()[:16]


def collect(paths):
    out = []
    for p in paths:
        p = Path(p)
        out += sorted(p.glob("*.log")) if p.is_dir() else [p]
    return out


def default_cache():
    build = ROOT / "build"
    return build / "liveness_cache.json" if build.is_dir() else None


def run(paths, spec_path=SPEC, cache_path=None, out=sys.stdout, root=ROOT):
    spec, rules = load_spec(spec_path)
    profiles = Profiles(root)
    files = collect(paths)
    key_spec = _spec_hash(spec_path)
    cache = {}
    if cache_path and Path(cache_path).exists():
        try:
            cache = json.loads(Path(cache_path).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cache = {}
    results, t_start, t_last, hits = {}, time.time(), time.time(), 0
    for n, path in enumerate(files, 1):
        st = path.stat()
        key = f"{path.resolve()}|{st.st_size}|{int(st.st_mtime)}|{key_spec}"
        if key in cache:
            results[str(path)] = cache[key]
            hits += 1
        else:
            text = path.read_text(encoding="utf-8", errors="replace")
            results[str(path)] = cache[key] = check_text(text, rules, profiles)
        now = time.time()
        if len(files) > 1 and (now - t_last >= 30 or n == len(files) or n % 10 == 0):
            rate = n / max(now - t_start, 1e-6)
            dead = sum(1 for r in results.values() if summary_counts(r)[DEAD])
            print(f"liveness progress: {n}/{len(files)} logs, {rate:.1f}/s, ETA {(len(files) - n) / rate:.0f} s, "
                  f"cached {hits}, logs with DEAD rows {dead}", file=out, flush=True)
            t_last = now
    if cache_path:
        try:
            Path(cache_path).write_text(json.dumps(cache), encoding="utf-8")
        except OSError:
            pass
    return spec, rules, results


def table_lines(rules, results):
    """Per row over many logs: logs ALIVE / DEAD / UNPROVEN (worst preset of each log)."""
    lines = [f"{'row':24} {'alive':>5} {'dead':>5} {'unpr':>5} {'n/a':>5}  dead logs (first 3)"]
    for rule in rules:
        c = {ALIVE: [], DEAD: [], UNPROVEN: [], None: []}
        for name, result in results.items():
            c[log_status(result.get(rule.id, {}))].append(Path(name).stem)
        lines.append(f"{rule.id:24} {len(c[ALIVE]):5} {len(c[DEAD]):5} {len(c[UNPROVEN]):5} {len(c[None]):5}  "
                     + ", ".join(c[DEAD][:3]))
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="game logs or directories of *.log")
    ap.add_argument("--spec", default=str(SPEC))
    ap.add_argument("--root", default=str(ROOT), help="tree whose bg3rtx.ini, data/release.ini and settings schema apply")
    ap.add_argument("--json", help="write the full result here")
    ap.add_argument("--gate", action="store_true", help="exit 1 when any row is DEAD")
    ap.add_argument("--cache", default=None, help="result cache (default build/liveness_cache.json for directories)")
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--quiet", action="store_true", help="one summary line per log")
    ap.add_argument("--rules", action="store_true", help="check the spec against the presets (AGENTS.md rule), no logs")
    a = ap.parse_args(argv if argv is not None else sys.argv[1:] or ["--help"])
    if a.rules:
        problems = rule_problems(json.loads(Path(a.spec).read_text(encoding="utf-8")), Profiles(a.root))
        for p in problems:
            print("liveness rule:", p)
        print(f"liveness rules: {len(problems)} problems")
        return 1 if problems else 0
    many = len(collect(a.paths)) > 1
    cache = None if a.no_cache else (a.cache or (default_cache() if many else None))
    spec, rules, results = run(a.paths, a.spec, cache, root=a.root)
    dead = False
    for name, result in results.items():
        lines = report_lines(Path(name).name, result)
        dead |= summary_counts(result)[DEAD] > 0
        for line in (lines[:1] if a.quiet or many else lines):
            print(line, flush=True)
    if many:
        print()
        for line in table_lines(rules, results):
            print(line)
    if a.json:
        Path(a.json).write_text(json.dumps(results, indent=1), encoding="utf-8")
    if not results:
        print("liveness: no logs", file=sys.stderr)
        return 2
    return 1 if (a.gate and dead) else 0


if __name__ == "__main__":
    sys.exit(main())
