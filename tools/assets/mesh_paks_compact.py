"""Additive B3PAKS1 v3 -> v4 compaction. No decoder, geometry retained only as SHA-256.

The immutable source shards are checked against their metadata while streaming.
The complete compact manifest is published last. No source/cache dependency remains.
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path


def compact(summary, output):
    summary, output = Path(summary), Path(output)
    manifest = output.with_suffix('.manifest')
    report_path = output.with_suffix('.json')
    if any(p.exists() for p in (output, manifest, report_path)):
        raise FileExistsError('use a new output prefix')
    source = json.loads(summary.read_text())
    batches = source['batches']
    rows = colors = textures = source_bytes = 0
    started = heartbeat = time.monotonic()
    delete = [str(summary)]
    with output.open('xb') as dst:
        dst.write(struct.pack('<8sII', b'B3PAKS1\0', 4, 0))
        for number, batch in enumerate(batches):
            batch_path = summary.parent / batch
            metadata = json.loads(batch_path.read_text())
            src_manifest = batch_path.with_suffix('.manifest')
            delete.extend((str(batch_path), str(src_manifest)))
            pending = iter(metadata['rows'])
            for name in src_manifest.read_text().splitlines():
                path = summary.parent / name
                delete.append(str(path))
                source_bytes += path.stat().st_size
                with path.open('rb') as src:
                    def take(n):
                        data = src.read(n)
                        if len(data) != n:
                            raise ValueError(f'truncated {path}')
                        return data
                    magic, version, count = struct.unpack('<8sII', take(16))
                    if magic != b'B3PAKS1\0' or version != 3:
                        raise ValueError('expected complete v3 shards')
                    for _ in range(count):
                        row = next(pending)
                        fixed = take(840)
                        cid, count, span = struct.unpack_from('<QII', fixed)
                        if (cid != int(row['content_id'], 16) or count != row['index_count']
                                or span != row['vertex_span'] or fixed[804:].decode() != row['material_id']):
                            raise ValueError('shard/metadata row mismatch')
                        if struct.unpack('<I', take(4))[0] != count:
                            raise ValueError('bad geometry count')
                        geometry = hashlib.sha256(take(count * 20)).digest()
                        if geometry.hex() != row['geometry_sha256']:
                            raise ValueError('geometry digest mismatch')
                        layout, color_count = struct.unpack('<II', take(8))
                        if layout not in (0, 1) or color_count != (count if layout else 0):
                            raise ValueError('invalid COLOR layout')
                        color = hashlib.sha256(take(color_count * 16)).digest() if layout else bytes(32)
                        if layout != row.get('color_layout', 0) or (layout and color.hex() != row['color_sha256']):
                            raise ValueError('COLOR digest mismatch')
                        refs = sorted((key, value['id']) for key, value in row['textures'].items())
                        if len(refs) > 128:
                            raise ValueError('too many texture refs')
                        dst.write(fixed + struct.pack('<I', layout) + geometry + color + struct.pack('<I', len(refs)))
                        for parameter, uuid in refs:
                            raw = parameter.encode('ascii')
                            if not 0 < len(raw) <= 128 or len(uuid) != 36:
                                raise ValueError('invalid texture ref')
                            dst.write(struct.pack('<I', len(raw)) + raw + uuid.encode('ascii'))
                        rows += 1
                        colors += bool(layout)
                        textures += len(refs)
                    if src.read(1):
                        raise ValueError('trailing shard bytes')
            if next(pending, None) is not None:
                raise ValueError('unconsumed metadata rows')
            now = time.monotonic()
            if now - heartbeat >= 25 or number + 1 == len(batches):
                elapsed = now - started
                print(f'compact {number+1}/{len(batches)} batches; {rows} rows; '
                      f'{source_bytes/2**30:.2f} GiB read; ETA {elapsed/(number+1)*(len(batches)-number-1):.0f}s; failures 0', flush=True)
                heartbeat = now
        if rows != source['counts']['decoded']:
            raise ValueError('row conservation failed')
        dst.seek(12)
        dst.write(struct.pack('<I', rows))
    report = dict(format='B3PAKS1 v4', source=str(summary), rows=rows, color_rows=colors,
                  texture_refs=textures, binary_bytes=output.stat().st_size,
                  source_binary_bytes=source_bytes, seconds=time.monotonic()-started,
                  source_files_coordinator_may_delete=delete,
                  qualification='offline candidates; full ordered SHA-256 and native publication gates required')
    with report_path.open('x') as dst:
        json.dump(report, dst, indent=2)
    with manifest.open('x') as dst:
        dst.write(output.name + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'source_files_coordinator_may_delete'}, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary', required=True)
    parser.add_argument('--out', required=True, help='new mesh_paks_cov_paks2_*.bin')
    args = parser.parse_args()
    compact(args.summary, args.out)
