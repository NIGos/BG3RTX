"""Exact origin finalization decision, including IEEE non-finite fallbacks.

The ray query, normal calculation, tolerance and physical hair origin are never
changed. This fixture tests only the decision to read a side-effect-free texture.
"""
import itertools
import json
import math
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def finite(a):
    return all(math.isfinite(x) for x in a)


def original(o, view, normal):
    o = list(o)
    if dot(o[:3], o[:3]) < 0.5:
        o[:3] = normal
    elif dot(o[:3], view) < 0:
        o[:3] = [-x for x in o[:3]]
    if not finite(o):
        o = [*normal, 1000.0]
    return o


def deferred(o, view, normal, stats):
    o = list(o)
    degenerate = dot(o[:3], o[:3]) < 0.5
    if not degenerate and dot(o[:3], view) < 0:
        o[:3] = [-x for x in o[:3]]
    load = degenerate or not finite(o) or stats != 0
    # Poison would reveal accidental consumption on the non-loading path.
    ns = normal if load else [math.nan] * 3
    if degenerate:
        o[:3] = ns
    if not finite(o):
        o = [*ns, 1000.0]
    return o, load


def bits(a):
    return b"".join(struct.pack("f", float(x)) for x in a)


class OriginLazyNormalTest(unittest.TestCase):
    def test_named_decisions(self):
        cases = json.loads((ROOT / "tests/fixtures/origin_lazy_normal.json").read_text())["cases"]
        for case in cases:
            with self.subTest(case=case["name"]):
                o, v, n = ([float(x) for x in case[k]] for k in ("origin", "view", "normal"))
                actual, load = deferred(o, v, n, case["stats"])
                self.assertEqual(load, case["load"])
                self.assertEqual(bits(actual), bits(original(o, v, n)))
                self.assertEqual(bits(actual), bits(case["output"]))

    def test_cartesian_edge_cases(self):
        values = [0.0, -0.0, 0.25, -1.0, 1.0, math.inf, -math.inf, math.nan]
        normals = [(0, 0, -1), (1, 0, 0), (math.nan, 0, 1), (math.inf, 0, 0)]
        for xyz, w, ns, stats in itertools.product(itertools.product(values, repeat=3),
                                                   [0, 1000, math.nan, math.inf], normals, [0, 1]):
            o = [*xyz, w]
            actual, _ = deferred(o, (0, 0, 1), ns, stats)
            self.assertEqual(bits(actual), bits(original(o, (0, 0, 1), ns)))

    def test_shader_and_host_decision(self):
        source = (ROOT / "shaders/gi/rt_origin.hlsl").read_text()
        self.assertIn("[[vk::constant_id(1027)]] const uint RT_ORIGIN_LAZY_NORMAL = 0u;", source)
        self.assertIn("const bool degenerate = dot(o.xyz, o.xyz) < 0.5;", source)
        self.assertIn("if (!degenerate && dot(o.xyz, V) < 0.0) o.xyz = -o.xyz;", source)
        self.assertIn("if (degenerate || !all(isfinite(o)) || pc.rt_stats != 0u)", source)
        self.assertIn("if (degenerate) o.xyz = Ns;", source)
        self.assertEqual(source.count("if (!all(isfinite(o))) o = float4(Ns, kOriginNoHit);"), 2)
        host = (ROOT / "core/gi.cpp").read_text()
        self.assertIn('specs.add(sv::kSpecIdOriginLazyNormal, host->cfg_int("rt_origin_lazy_normal", 0) != 0 ? 1u : 0u);', host)
        self.assertIn("slot_key += specs.key();", host)


if __name__ == "__main__":
    unittest.main()
