"""Real Vulkan R11G11B10 oracle, compiling the production function.

No game/window/queue. --characterize reports unfixed boundary mismatches.
"""
import argparse
import collections
import math
from pathlib import Path
import re
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (SourceTree, compile_requests, compile_many, lower_priority,
                                   Toolchain, Cache, identity, identical)
from tools.release_shader_equivalence import Normalizer

NORMAL_CONDITION = 'all(v >= 0.00006103515625) && all(v <= float3(65024.0, 65024.0, 64512.0))'
ORIGINAL_FUNCTION = '''float3 trunc_r11g11b10(float3 v) {
  uint3 u = asuint(max(v, 0.0));
  u &= uint3(~((1u << 17) - 1u), ~((1u << 17) - 1u), ~((1u << 18) - 1u));
  return asfloat(u);
}'''


def reference(bits, mantissa):
    """Independent numeric format model: decode, quantize in units of its ULP.

    NaN payload handling is the observed RTX 5090 conversion, checked bitwise.
    """
    value = struct.unpack('<f', struct.pack('<I', bits))[0]
    shift = 23 - mantissa
    if math.isnan(value):
        payload = max(1, (bits & 0x7fffff) >> shift)
        return 0x7f800000 | (payload << shift)
    if value <= 0:
        return 0
    if math.isinf(value):
        return 0x7f800000
    maximum = (2 - 2 ** -mantissa) * 2 ** 15
    value = min(value, maximum)
    exponent = max(-14, math.frexp(value)[1] - 1)
    step = 2 ** (exponent - mantissa)
    return struct.unpack('<I', struct.pack('<f', math.floor(value / step) * step))[0]


class ReferenceTest(unittest.TestCase):
    def test_format_limits(self):
        for m, top in ((6, 65024.0), (5, 64512.0)):
            word = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
            self.assertEqual(reference(word(1e10), m), word(top))
            self.assertEqual(reference(word(2 ** -14), m), word(2 ** -14))
            self.assertEqual(reference(word(2 ** (-14-m)), m), word(2 ** (-14-m)))
            self.assertEqual(reference(word(2 ** (-15-m)), m), 0)
            self.assertEqual(reference(0x7f800000, m), 0x7f800000)
            self.assertEqual(reference(0xff800000, m), 0)
            self.assertEqual(reference(0x7f800001, m), 0x7f800000 | (1 << (23-m)))

    def test_corpus_covers_limits_and_neighbours(self):
        words = set(corpus())
        for boundary in (0x35800000, 0x36000000, 0x38800000, 0x477c0000, 0x477e0000):
            self.assertTrue({boundary-1, boundary, boundary+1} <= words)


def equivalence(baseline=None):
    """Release SPIR-V proof under the declared normal-input domain and key OFF.

    Only the exact normal-range guard is assumed true in the domain proof;
    its body and the entire remaining RR pipeline compile as production.
    Full-range equality is intentionally NOT claimed for the boundary fix.
    """
    tool, cache = Toolchain(), Cache()
    new = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    requests = sorted(set(r for r in new.requests()
                          if 'shaders/rr_post.hlsl' in dict(new.closure('shaders/' + r.split('?')[0]))))
    files = {p: new.read(p) for p in new.ids}
    source = files['shaders/rr_post.hlsl'].decode().replace('\r\n', '\n')
    assert source.count(function()) == 1
    if baseline:
        old = SourceTree.commit(baseline).with_sharc(tool.sharc)
    else:
        # Keep the regression valid after merge: compare only this function
        # with its original implementation in the otherwise current tree.
        original = dict(files)
        original['shaders/rr_post.hlsl'] = source.replace(function(), ORIGINAL_FUNCTION).encode()
        old = SourceTree.memory(original, 'original-emulation').with_sharc(tool.sharc)
    assert source.count(NORMAL_CONDITION) == 1
    normal = dict(files)
    normal['shaders/rr_post.hlsl'] = source.replace(NORMAL_CONDITION, 'true').encode()
    domain = SourceTree.memory(normal, 'normal-domain').with_sharc(tool.sharc)
    flag = '(pc.flags & 2u) != 0u'
    assert source.count(flag) == 1
    off_new = dict(files)
    off_new['shaders/rr_post.hlsl'] = source.replace(flag, 'false').encode()
    off_old = {p: old.read(p) for p in old.ids}
    assert off_old['shaders/rr_post.hlsl'].count(flag.encode()) == 1
    off_old['shaders/rr_post.hlsl'] = off_old['shaders/rr_post.hlsl'].replace(flag.encode(), b'false')
    pairs = [(old, domain), (SourceTree.memory(off_old, 'old-off').with_sharc(tool.sharc),
                             SourceTree.memory(off_new, 'new-off').with_sharc(tool.sharc))]
    normalizer = Normalizer(tool, cache.dir / 'release_equivalence')
    actual = compile_requests(new, requests, tool=tool, cache=cache, jobs=1, validate=True)
    for result in actual.values():
        assert result.ok and result.valid, result.log
    with tempfile.TemporaryDirectory(prefix='rr-r11-eq-') as tmp:
        for a, b in pairs:
            results = compile_many([(t, r) for t in (a, b) for r in requests], tool=tool, cache=cache, jobs=1)
            for r in requests:
                left, right = results[(a.label, r)], results[(b.label, r)]
                assert left.ok and right.ok, left.log + right.log
                assert [e.name for e in left.entries] == [e.name for e in right.entries]
                for entry in left.entries:
                    assert normalizer.get(left.blob(entry.name), {}, Path(tmp), 'a') == normalizer.get(right.blob(entry.name), {}, Path(tmp), 'b'), (r, entry.name, b.label)
                if a is old:
                    for entry in left.entries:
                        if entry.name not in ('final', 'check', 'temporal_final'):
                            assert left.blob(entry.name) == actual[r].blob(entry.name), (r, entry.name)
            print(f'Release equivalence {a.label} / {b.label}: {len(requests)} RR requests PASS', flush=True)
    other = sorted(set(new.requests()) - set(requests))
    checked = identity(old, new, other, tool=tool, cache=cache, jobs=1)
    assert all(identical(state) for _, state, _ in checked), [row for row in checked if not identical(row[1])]
    assert old.read('shaders/variants.txt') == new.read('shaders/variants.txt')
    print(f'Ordinary requests: {len(other)} byte-identical; manifest delta=0', flush=True)
    for tree in (old, new, domain, pairs[1][0], pairs[1][1]):
        tree.cleanup()


def corpus():
    # Every positive finite FP16 value (chain storage), every R11/R10
    # boundary and its FP32 neighbours, negative counterparts and specials.
    words = {0, 0x80000000, 1, 0x7f7fffff, 0x7f800000, 0xff800000,
             0x7fc00000, 0x7fffffff, 0xffc00000, 0x7f800001}
    rng = random.Random(110510)
    words.update(rng.getrandbits(32) for _ in range(4096))
    for sign in (0, 0x80000000):
        for shift in (17, 18):
            for payload in range(1, 1 << (23-shift)):
                for neighbour in (-1, 0, 1):
                    words.add(sign | 0x7f800000 | ((payload << shift) + neighbour))
    for boundary in (0x477c0000, 0x477e0000, 0x47800000):
        words.update((boundary-1, boundary, boundary+1))
    for h in range(0x7c00):
        words.add(struct.unpack('<I', struct.pack('<f', struct.unpack('<e', struct.pack('<H', h))[0]))[0])
    for mantissa in (5, 6):
        for encoded in range(1, 31 << mantissa):
            e, m = divmod(encoded, 1 << mantissa)
            value = (m * 2.0 ** (-14 - mantissa) if e == 0 else
                     (1 + m / (1 << mantissa)) * 2.0 ** (e - 15))
            bits = struct.unpack('<I', struct.pack('<f', value))[0]
            words.update((bits - 1, bits, bits + 1))
    words.update(w | 0x80000000 for w in tuple(words) if w < 0x7f800000)
    return sorted(words)


def function():
    text = (ROOT / 'shaders/rr_post.hlsl').read_text()
    return re.search(r'float3 trunc_r11g11b10\(float3 v\) \{.*?\n\}', text, re.S).group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, nargs='?', default=ROOT / 'bin/rr_r11_fixture.exe')
    parser.add_argument('--characterize', action='store_true')
    parser.add_argument('--baseline', help='historical branch baseline for the pre-merge proof')
    args = parser.parse_args()
    lower_priority()
    if not unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ReferenceTest)).wasSuccessful():
        return 1
    source = function() + '''
[[vk::binding(0, 0)]] StructuredBuffer<uint> Input;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> Output;
[[vk::binding(2, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> Image;
struct Push { uint count; };
[[vk::push_constant]] Push pc;
[numthreads(64, 1, 1)]
void store(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= pc.count) return;
  float3 v = asfloat(uint3(Input[i], Input[(i+1)%pc.count], Input[(i+2)%pc.count]));
  Image[uint2(i % 256, i / 256)] = v;
  uint3 q = asuint(trunc_r11g11b10(v));
  Output[6*i] = q.x; Output[6*i+1] = q.y; Output[6*i+2] = q.z;
}
[numthreads(64, 1, 1)]
void load(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= pc.count) return;
  uint3 q = asuint(Image[uint2(i % 256, i / 256)]);
  Output[6*i+3] = q.x; Output[6*i+4] = q.y; Output[6*i+5] = q.z;
}
'''
    tree = SourceTree.memory({'shaders/fixture.hlsl': source.encode()})
    result = compile_requests(tree, ['fixture.hlsl'], jobs=1, validate=True)['fixture.hlsl']
    if not result.ok or not result.valid:
        raise RuntimeError(result.log)
    values = corpus()
    with tempfile.TemporaryDirectory(prefix='rr-r11-') as tmp:
        tmp = Path(tmp)
        (tmp / 'in').write_bytes(struct.pack(f'<{len(values)}I', *values))
        run = subprocess.run([str(args.exe.resolve()), str(tmp / 'in'), str(result.path('store')),
                              str(result.path('load')), str(tmp / 'out')])
        if run.returncode:
            return run.returncode
        data = (tmp / 'out').read_bytes()
    assert len(data) == len(values) * 24
    mismatches = collections.Counter()
    examples = {}
    normal_changes = 0
    oracle_errors = 0
    for index, row in enumerate(struct.iter_unpack('<6I', data)):
        bits = values[index]
        if args.characterize and ((bits & 0x7f800000) == 0x7f800000 or bits in (0x477dffff, 0x477e0000, 0x477e0001, 0x47800000)):
            print(f'boundary {bits:08x}: image=' + ','.join(f'{w:08x}' for w in row[3:]), flush=True)
        for ch, (actual, expected) in enumerate(zip(row[:3], row[3:])):
            bits = values[(index+ch) % len(values)]
            oracle_errors += expected != reference(bits, 6 if ch < 2 else 5)
            limit = 0x477e0000 if ch < 2 else 0x477c0000
            kind = ('negative' if bits >> 31 and bits <= 0xff800000 else
                    'special' if bits >= 0x7f800000 else
                    'subnormal' if bits < 0x38800000 else
                    'overflow' if bits > limit else 'normal')
            old = (bits if bits < 0x80000000 else 0) & (0xfffe0000 if ch < 2 else 0xfffc0000)
            if kind == 'normal' and actual != old:
                normal_changes += 1
            if actual != expected:
                mismatches[kind] += 1
                examples.setdefault(kind, f'in={bits:08x} ch={ch} emulated={actual:08x} image={expected:08x}')
    print(f'{len(values)} inputs, {3*len(values)} channel comparisons; normal changes={normal_changes}; mismatches={dict(mismatches)}', flush=True)
    for kind, example in examples.items():
        print(f'{kind}: {example}', flush=True)
    print(f'Independent numeric oracle mismatches={oracle_errors}', flush=True)
    if not args.characterize and not (normal_changes or mismatches or oracle_errors):
        equivalence(args.baseline)
    return int(bool(normal_changes or oracle_errors or (mismatches and not args.characterize)))


if __name__ == '__main__':
    sys.exit(main())
