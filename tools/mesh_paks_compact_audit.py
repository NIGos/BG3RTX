"""Read-only replay UV candidate audit; XFB/pool IDs are NOT native draw IDs."""
import argparse
import hashlib
import json
import struct
import re
from collections import Counter
from collections import defaultdict
from pathlib import Path

import numpy as np


def audit_log(text, index):
    """Only infer facts present in the log; repeated draws are not pixel weights."""
    readbacks = Counter()
    missing_counts = Counter()
    for count, status in re.findall(r'\[hit material paks UV\] (\d+) corners: (unique|missing|ambiguous) candidate', text):
        readbacks[status] += 1
        if status == 'missing':
            missing_counts['count_present' if int(count) in index else 'count_absent'] += 1
    windows = []
    pattern = (r'\[hit material paks\] frame (\d+) enabled ([01]): exact (\d+)/(\d+) attempts; '
               r'records (\d+) basis ready (\d+)')
    for values in re.findall(pattern, text):
        windows.append(dict(zip(('frame', 'enabled', 'exact', 'draws', 'records', 'basis_ready'), map(int, values))))
    joins = re.findall(r'\[hit material paks join\] ([^\r\n]+)', text)
    last = dict((k, int(v)) for k, v in re.findall(r'(\w+) (\d+)', joins[-1])) if joins else {}
    return dict(scope='logged readback completions and cumulative draw observations; not unique meshes or pixels',
                readbacks=dict(readbacks), missing_readbacks=dict(missing_counts), windows=windows,
                last_join=last,
                missing_attempt_percent=100*last['missing']/last['attempts'] if last.get('attempts') else None,
                missing_draw_percent=100*last['missing']/last['draws'] if last.get('draws') else None,
                screen_share=None, missing_family_shares=None,
                limits='No missing-draw VS/FS, UV values, resource IDs, per-draw counts or pixel weights in this log. '
                       'Count present plus missing UV does not prove mesh absence: UV decode/order/submesh differences remain possible.')


def candidates(path):
    data = Path(path).read_bytes()
    magic, version, count = struct.unpack_from('<8sII', data)
    if magic != b'B3PAKS1\0' or version != 4:
        raise ValueError('expected v4')
    at = 16
    result = defaultdict(list)
    for _ in range(count):
        cid, n, span, flags = struct.unpack_from('<QIII', data, at)
        uv = np.frombuffer(data, '<f4', 64, at+548).copy()
        positions = np.frombuffer(data, '<u4', 131, at+24)[3:].reshape(32,4)[:,1:].copy().view('<f4')
        material = data[at+804:at+840].decode()
        layout = struct.unpack_from('<I', data, at+840)[0]
        geometry = data[at+844:at+876].hex()
        color = data[at+876:at+908].hex()
        ntex = struct.unpack_from('<I', data, at+908)[0]
        at += 912
        refs = []
        for t in range(ntex):
            length = struct.unpack_from('<I', data, at)[0]
            at += 4
            refs.append(data[at:at+length+36])
            at += length+36
        result[n].append(dict(cid=cid, span=span, flags=flags, uv=uv, material=material,
                              geometry=geometry, color=color, layout=layout, refs=refs, positions=positions))
    if at != len(data):
        raise ValueError('trailing bytes')
    return result


def audit(pack, index):
    pack = Path(pack)
    report = dict(pack=pack.name, scope='ordered object-space pool only; not hit weighted',
                  examined=0, missing_count=0, missing_uv=0, unique=0, ambiguous=0,
                  exact_ordered_witness=0, unsupported=0, proven_runtime_joins=0, position_examples=[], sampled_position_bit_equal=0)
    pool = None
    for line in (pack/'manifest.txt').read_text().splitlines():
        fields = line.split()
        if fields[:3] == ['buffer', '1', '8'] and len(fields) == 5:
            pool = pack/fields[4]
        if fields[:3] == ['range', '1', '8']:
            report['unsupported_sparse_pool'] = True
            return report
    if pool is None:
        raise ValueError('pool missing')
    uv_arrays = {n: np.array([r['uv'] for r in rows]) for n, rows in index.items()}
    with pool.open('rb') as stream:
        for base, meta, record, tris in struct.iter_unpack('<4I', (pack/'s1b7.bin').read_bytes()):
            if not meta & (1 << 8):
                continue
            stride, n = meta & 255, tris*3
            if stride < 24 or meta & (1 << 9) or not n or base+n*stride > pool.stat().st_size:
                report['unsupported'] += 1
                continue
            report['examined'] += 1
            rows = index.get(n)
            if not rows:
                report['missing_count'] += 1
                continue
            got = []
            for s in range(32):
                stream.seek(base+(s*(n-1)//31)*stride+16)
                got.extend(struct.unpack('<2f', stream.read(8)))
            selected = np.flatnonzero(np.all(np.abs(uv_arrays[n]-got) <= 1e-3, axis=1))
            if not len(selected):
                report['missing_uv'] += 1
                continue
            contracts = {(rows[i]['cid'], rows[i]['span'], rows[i]['flags'], rows[i]['material'],
                          rows[i]['geometry'], rows[i]['color'], rows[i]['layout'], tuple(rows[i]['refs']),
                          rows[i]['uv'].tobytes()) for i in selected}
            report['unique' if len(contracts) == 1 else 'ambiguous'] += 1
            if len(contracts) != 1:
                continue
            stream.seek(base)
            raw = stream.read(n*stride)
            corners = np.empty((n, 5), '<f4')
            corners[:, :3] = np.ndarray((n, 3), '<f4', raw, strides=(stride, 4))
            corners[:, 3:] = np.ndarray((n, 2), '<f4', raw, offset=16, strides=(stride, 4))
            sample_positions=corners[np.arange(32)*(n-1)//31,:3]
            expected=rows[selected[0]]['positions']
            report['sampled_position_bit_equal'] += sample_positions.tobytes()==expected.tobytes()
            if len(report['position_examples'])<4:
                error=np.abs(sample_positions-expected)
                report['position_examples'].append(dict(material=rows[selected[0]]['material'], max_abs_error=float(error.max()), pool_first=sample_positions[0].tolist(), pak_first=expected[0].tolist(), max_coordinate=float(np.abs(expected).max())))
            report['exact_ordered_witness'] += hashlib.sha256(corners.tobytes()).hexdigest() == rows[selected[0]]['geometry']
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalogue', required=True, type=Path)
    parser.add_argument('--pack', action='append', default=[], type=Path)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if not args.pack and not args.log:
        parser.error('provide --pack or --log')
    index = candidates(args.catalogue)
    reports = []
    for pack in args.pack:
        reports.append(audit(pack, index))
        print(json.dumps(reports[-1]), flush=True)
    with args.out.open('x') as dst:
        result = dict(status='OFFLINE_CANDIDATES_ONLY', packs=reports)
        if args.log:
            result['log'] = audit_log(args.log.read_text(errors='replace'), index)
            result['catalogue'] = dict(rows=sum(map(len,index.values())), counts=len(index),
                flag_histogram=dict(Counter(row['flags'] for rows in index.values() for row in rows)))
        json.dump(result, dst, indent=2)
