"""Decode catalogue GR2s into additive, local-only pak material join candidates.

python -m tools.assets.mesh_paks --catalogue <v2.json> --cache <workspace/cache>
    --out <NEW assetdb/mesh_paks_cov_paks.json> --lslib <v1.20.4 Tools> [--limit 30]

Hashes select candidates only. Neither sampled geometry nor UV is an equality
proof. Runtime must independently verify geometry, basis and texture generations.
Larian-derived outputs are generated on the player's PC and are never shipped.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import queue
from collections import Counter

import numpy as np

from .locate import game_data, lslib_tools
from .lspk import PackageSet
from .mesh_materials import write_new
from ..hair_groom.asset import content_id

FORMAT = 'BG3RTX_MESH_PAKS_V1'
VERSION = 'mesh-paks-2-color'
ROOT = Path(__file__).resolve().parents[2]


def say(s):
    print(s, flush=True)


def read_dump(path):
    """Strict bounds-checked GR2P decoder; retain native group/index ordering."""
    raw = Path(path).read_bytes()
    at = 0

    def take(n):
        nonlocal at
        if n < 0 or at + n > len(raw):
            raise ValueError('truncated GR2P dump')
        b = raw[at:at+n]
        at += n
        return b

    def u32():
        return struct.unpack('<I', take(4))[0]

    if take(4) != b'GR2P':
        raise ValueError('unsupported GR2P magic')
    version = u32()
    if version not in (1, 2):
        raise ValueError('unsupported GR2P version')
    result = {}
    for _ in range(u32()):
        name = take(u32()).decode('utf-8')
        nv, ni, ng = u32(), u32(), u32()
        layout = u32() if version == 2 else 0
        if layout not in (0, 1):
            raise ValueError('ambiguous native COLOR_0 layout')
        verts = np.frombuffer(take(nv * 20), '<f4').reshape(nv, 5)
        colors = np.frombuffer(take(nv * 16), '<f4').reshape(nv, 4) if layout else None
        if colors is not None and (not np.isfinite(colors).all() or np.any((colors < 0) | (colors > 1))):
            raise ValueError('non-finite/out-of-range native COLOR_0')
        idx = np.frombuffer(take(ni * 4), '<u4')
        groups = np.frombuffer(take(ng * 12), '<u4').reshape(ng, 3)
        if name in result:
            raise ValueError('duplicate native mesh name')
        if ni % 3 or not ni or idx.max() >= nv or not np.isfinite(verts).all():
            raise ValueError('unsupported/non-finite geometry')
        for _, first, count in groups:
            if not count or int(first) + int(count) > ni // 3:
                raise ValueError('invalid native triangle group')
        result[name] = (verts, idx, groups, colors)
    if at != len(raw):
        raise ValueError('trailing GR2P data')
    return result


def mesh_object(object_id):
    """VisualBank's file.mesh.ExportOrder identity (suffix is NOT a group)."""
    parts = object_id.split('.')
    if len(parts) != 3 or not parts[1] or not parts[2].isdigit():
        raise ValueError('unsupported ObjectID')
    return parts[1], int(parts[2])


def signature(verts, indices):
    idx = np.asarray(indices, '<u4').ravel()
    if not len(idx) or len(idx) % 3 or idx.max() >= len(verts):
        raise ValueError('invalid triangle list')
    if not np.isfinite(verts).all():
        raise ValueError('non-finite geometry')
    cid, words = content_id(verts[:, :3], idx)
    sample = idx[np.arange(32, dtype=np.int64) * (len(idx) - 1) // 31]
    uv = np.ascontiguousarray(verts[sample, 3:5], '<f4')
    # Full ordered corners are retained for independent equality verification;
    # SHA-256 alone is not used to qualify a runtime material.
    full = np.ascontiguousarray(verts[idx], '<f4').tobytes()
    return dict(content_id=f'{cid:016x}', canonical_words=words, index_count=len(idx),
                vertex_span=int(idx.max())-int(idx.min())+1,
                uv=uv.ravel().tolist(), geometry_sha256=hashlib.sha256(full).hexdigest()), full


def nodes(native):
    if isinstance(native, dict):
        yield native
        for v in native.values():
            yield from nodes(v)
    elif isinstance(native, list):
        for v in native:
            yield from nodes(v)


def material_info(doc, material_id):
    mat = doc['materials'][material_id]
    template = doc.get('templates', {}).get(mat['source'], {}).get('template')
    if not template:
        raise ValueError('material template missing')
    flags = int(template['flags'])
    textures = {}
    for n in nodes(mat['native']):
        if n.get('_') in ('Texture2DParameters', 'VirtualTextureParameters'):
            if n.get('Enabled') is False:
                continue
            tid, parameter = n.get('ID'), n.get('ParameterName')
            tex = doc['textures'].get(tid, {})
            if not tid or not parameter or not tex:
                raise ValueError('unresolved material texture')
            if any(k.endswith(':' + tid) for k in doc.get('conflicts', {})):
                raise ValueError('ambiguous material texture')
            entry = dict(id=tid, bank=tex['bank'], native=tex['native'])
            if parameter in textures and textures[parameter] != entry:
                raise ValueError('conflicting texture parameter')
            textures[parameter] = entry
    return dict(material_id=material_id, template=mat['source'], textures=textures,
                two_sided=bool(flags & (1 << 15)), uses_vertex_color=bool(flags & (1 << 17)),
                parameters=mat['native'], basis_verified=False,
                vertex_color_verified=False, texture_views_verified=False)


def select_rows(doc, match=('lava', 'tileset', 'hlod')):
    assignments, poisoned = {}, set()
    for r in doc['rows']:
        # Native mesh names are unique. ExportOrder is exporter metadata, not a
        # triangle group; conflicting assignments to this mesh poison all LODs.
        try:
            key = (r['source'], mesh_object(r['object_id'])[0])
        except ValueError:
            continue
        assignments.setdefault(key, set()).add(r['material_id'])
        if r.get('ambiguous') or not r.get('material_resolved'):
            poisoned.add(key)
    poisoned.update(k for k, v in assignments.items() if len(v) != 1)
    wanted = {k for k, m in doc['materials'].items()
              if any(s.lower() in m['source'].lower() for s in match)}
    result = []
    for r in doc['rows']:
        try:
            key = (r['source'], mesh_object(r['object_id'])[0])
        except ValueError:
            continue
        if r['material_id'] in wanted and key not in poisoned:
            result.append(r)
    return result


def catalogue_plan(doc, match=('lava', 'tileset', 'hlod')):
    """Inventory all requested rows before decoding; blocked work is not rejection."""
    selected = select_rows(doc, match)
    wanted = {k for k, m in doc['materials'].items()
              if any(s.lower() in m['source'].lower() for s in match)}
    candidates = [r for r in doc['rows'] if r['material_id'] in wanted]
    families = {}
    for row in candidates:
        family = Path(doc['materials'][row['material_id']]['source']).name
        f = families.setdefault(family, dict(candidates=0, eligible=0))
        f['candidates'] += 1
    for row in selected:
        families[Path(doc['materials'][row['material_id']]['source']).name]['eligible'] += 1
    return dict(format=FORMAT, generated_locally=True, redistributable=False,
                qualification='inventory only; no runtime joins or quality qualification',
                counts=dict(candidates=len(candidates), eligible=len(selected),
                            rejected_metadata=len(candidates)-len(selected), decoded=0, joined=0,
                            unprocessed=len(selected), sources=len({r['source'] for r in selected})),
                families=families,
                sources=[dict(source=source, rows=count) for source, count in sorted(
                    __import__('collections').Counter(r['source'] for r in selected).items())])


def dump(lslib, source, output, pwsh):
    script = Path(__file__).with_name('mesh_paks_dump.ps1')
    # communicate timeout emits no hidden child work after returning.
    with subprocess.Popen([str(pwsh), '-NoProfile', '-NonInteractive', '-File', str(script),
                           '-Lslib', str(lslib), '-Gr2', str(source), '-Out', str(output)],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        try:
            stdout, _ = proc.communicate(timeout=300)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.communicate()
            raise RuntimeError('LSLib decode exceeded 300 s')
        if proc.returncode:
            raise RuntimeError('LSLib decode: ' + stdout.decode(errors='replace')[-500:])


class Decoder:
    """One native reader process per batch; same GR2P contract as one-shot dump."""
    def __init__(self, lslib, pwsh):
        self.proc = subprocess.Popen([str(pwsh), '-NoProfile', '-NonInteractive', '-File',
            str(Path(__file__).with_name('mesh_paks_dump.ps1')), '-Lslib', str(lslib), '-Server'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding='utf-8')
        self.lines = queue.Queue()
        def read():
            for line in self.proc.stdout:
                self.lines.put(line)
            self.lines.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def __call__(self, lslib, source, output, pwsh):
        self.proc.stdin.write(json.dumps(dict(source=str(source), output=str(output))) + '\n')
        self.proc.stdin.flush()
        try:
            line = self.lines.get(timeout=300)
        except queue.Empty:
            self.close()
            raise RuntimeError('LSLib decode exceeded 300 s')
        if line is None:
            raise RuntimeError('LSLib reader exited unexpectedly')
        try:
            result = json.loads(line)
        except ValueError as e:
            raise RuntimeError('LSLib protocol: ' + line[-500:]) from e
        if not result.get('ok'):
            raise RuntimeError('LSLib decode: ' + result.get('error', 'unknown error'))

    def close(self):
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill(); self.proc.wait()
        self.reader.join(timeout=5)


def binary_table(rows):
    # v3 retains native COLOR_0 in the same ordered-corner domain.
    data = bytearray(struct.pack('<8sII', b'B3PAKS1\0', 3, len(rows)))
    for r in rows:
        mid = r['material_id'].encode('ascii')
        if len(mid) != 36:
            raise ValueError('invalid material UUID')
        recipe = Path(r['template']).name == 'Engine_Default_HLOD_GM_VT.lsf'
        flags = int(r['two_sided']) | (int(r['uses_vertex_color']) << 1) | (int(recipe) << 2)
        data += struct.pack('<Q4I131I64f36s', int(r['content_id'], 16), r['index_count'],
                            r['vertex_span'], flags, 0, *r['canonical_words'], *r['uv'], mid)
        full = Path(r['full_geometry_cache']).read_bytes()
        if len(full) != r['index_count'] * 20 or hashlib.sha256(full).hexdigest() != r['geometry_sha256']:
            raise ValueError('full geometry stream missing or changed')
        data += struct.pack('<I', r['index_count']) + full
        layout = r.get('color_layout', 0)
        if layout not in (0, 1):
            raise ValueError('ambiguous COLOR_0 layout')
        color = Path(r['full_color_cache']).read_bytes() if layout else b''
        if layout and (len(color) != r['index_count'] * 16 or hashlib.sha256(color).hexdigest() != r['color_sha256']
                       or not np.isfinite(np.frombuffer(color, '<f4')).all()
                       or np.any((np.frombuffer(color, '<f4') < 0) | (np.frombuffer(color, '<f4') > 1))):
            raise ValueError('full COLOR_0 stream missing or changed')
        data += struct.pack('<II', layout, r['index_count'] if layout else 0) + color
    return bytes(data)


def write_sharded_tables(rows, output, max_bytes=480 * 1024 * 1024):
    """Stream immutable tables below the runtime 512 MB per-file parser bound.

    The caller publishes the JSON manifest last. Metadata alone stays in memory;
    each geometry/color stream is read, validated and released one row at a time.
    """
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    buckets, current, size = [], [], 16
    for row in rows:
        row_size = 840 + 4 + row['index_count'] * 20 + 8
        if row.get('color_layout', 0): row_size += row['index_count'] * 16
        if row_size + 16 > max_bytes:
            raise ValueError('one mesh exceeds shard byte budget; cannot split a mesh identity')
        if size + row_size > max_bytes and current:
            buckets.append(current); current, size = [], 16
        current.append(row); size += row_size
    if current or not buckets: buckets.append(current)
    names = [output.with_name(output.stem + f'_{i:04d}.bin') for i in range(len(buckets))]
    if any(path.exists() for path in names):
        raise ValueError('all binary shards must be NEW files')
    for number, (path, bucket) in enumerate(zip(names, buckets), 1):
        with path.open('xb') as stream:
            stream.write(struct.pack('<8sII', b'B3PAKS1\0', 3, len(bucket)))
            for row in bucket:
                stream.write(binary_table([row])[16:])
        say(f'[mesh paks] shard {number}/{len(names)}: {len(bucket)} rows, {path.stat().st_size} bytes')
    return [path.name for path in names]


def build(doc, ps, lslib, cache, pwsh='pwsh', limit=0, match=('lava', 'tileset', 'hlod'), start_source=0, decoder=None):
    decoder = decoder or dump
    cache = Path(cache); cache.mkdir(parents=True, exist_ok=True)
    groups = {}
    for row in select_rows(doc, match):
        groups.setdefault(row['source'], []).append(row)
    sources = sorted(groups, key=lambda s: (not any('lava' in doc['materials'][r['material_id']]['source'].lower() for r in groups[s]), s))
    if start_source < 0 or limit < 0:
        raise ValueError('source offset and limit must be nonnegative')
    total_sources = len(sources)
    sources = sources[start_source:start_source+limit if limit else None]
    selected_sources = set(sources)
    progress = dict(done=0, failures=0, hits=0, candidates=sum(len(groups[s]) for s in sources), decoded=0,
                    native_decoded=0, rejected_rows=0, source_failed_rows=0)
    start = time.monotonic(); stop = threading.Event()

    def report():
        elapsed = max(time.monotonic()-start, 1e-9)
        rate = progress['done']/elapsed
        say(f"[mesh paks] {progress['done']}/{len(sources)} GR2, {rate:.2f}/s, "
            f"ETA {(len(sources)-progress['done'])/max(rate,1e-9):.0f}s, decoded {progress['decoded']}, failures {progress['failures']}, cached {progress['hits']}")

    def heartbeat():
        while not stop.wait(25):
            report()

    thread = threading.Thread(target=heartbeat, daemon=True); thread.start()
    output, failures = [], []
    checkpoint_path = cache / ('batch-' + hashlib.sha256(json.dumps(sources).encode()).hexdigest()[:16] + '.checkpoint')
    decoder_identity = hashlib.sha256(Path(__file__).read_bytes()).digest()
    for dependency in ('LSLib.dll', 'granny2.dll'):
        dep = Path(lslib)/dependency
        if dep.is_file():
            decoder_identity += hashlib.sha256(dep.read_bytes()).digest()
    decoder_identity += hashlib.sha256(Path(__file__).with_name('mesh_paks_dump.cs').read_bytes()).digest()
    try:
        for source in sources:
            try:
                raw = ps.read(source)
                relevant = groups[source]
                contract = json.dumps([material_info(doc, r['material_id']) for r in relevant], sort_keys=True).encode()
                key = hashlib.sha256(raw + contract + json.dumps(relevant, sort_keys=True).encode() + VERSION.encode()
                                     + decoder_identity + Path(__file__).with_name('mesh_paks_dump.ps1').read_bytes()).hexdigest()
                record = cache / (key + '.json')
                if record.exists():
                    try:
                        saved = json.loads(record.read_text(encoding='utf-8'))
                        for cached_row in saved['rows']:
                            binary_table([cached_row])  # validate one geometry stream at a time
                    except (OSError, ValueError, KeyError, struct.error):
                        saved = None
                    if saved is not None:
                        output.extend(saved['rows']); failures.extend(saved['failures'])
                        progress['failures'] += len(saved['failures']); progress['hits'] += 1; progress['decoded'] += len(saved['rows'])
                        progress['native_decoded'] += saved.get('native_decoded', len(saved['rows']))
                        progress['rejected_rows'] += len(saved['failures'])
                        continue
                with tempfile.TemporaryDirectory(prefix='bg3-paks-') as tmp:
                    src, decoded = Path(tmp)/'input.gr2', Path(tmp)/'decoded.bin'
                    src.write_bytes(raw); decoder(lslib, src, decoded, pwsh)
                    meshes = read_dump(decoded)
                    built, rejected = [], []
                    native_decoded = 0
                    for r in relevant:
                        try:
                            name, export_order = mesh_object(r['object_id'])
                            verts, idx, native_groups, colors = meshes[name]
                            native_decoded += 1
                            # VisualBank rows address whole named meshes. Only
                            # a single full-range material group is supported;
                            # never mistake ExportOrder for a group ordinal.
                            if len(native_groups) != 1:
                                raise ValueError('multi-material native mesh unsupported')
                            material_index, first, count = map(int, native_groups[0])
                            if material_index != 0 or first != 0 or count * 3 != len(idx):
                                raise ValueError('native group does not cover the whole mesh')
                            sig, full = signature(verts, idx[first*3:(first+count)*3])
                            full_path = cache / (sig['geometry_sha256'] + '.corners')
                            if not full_path.exists() or full_path.read_bytes() != full:
                                # Workspace cache only, never shared assetdb.
                                full_path.write_bytes(full)
                            color_info = dict(color_layout=0, vertex_color_semantic=None)
                            if colors is not None:
                                color = np.ascontiguousarray(colors[idx], '<f4').tobytes()
                                color_hash = hashlib.sha256(color).hexdigest()
                                color_path = cache / (color_hash + '.colors')
                                if not color_path.exists() or color_path.read_bytes() != color:
                                    color_path.write_bytes(color)
                                color_info = dict(color_layout=1, vertex_color_semantic='COLOR_0_RGBA',
                                                  color_sha256=color_hash, full_color_cache=str(color_path.resolve()))
                            built.append(dict(**sig, **material_info(doc, r['material_id']), **color_info, source=source,
                                              object_id=r['object_id'], lod=r['lod'],
                                              export_order=export_order,
                                              identity='source + unique native mesh name; single full-range material group',
                                              native_export_order_verified=False, native_lod_verified=False,
                                              full_geometry_cache=str(full_path.resolve())))
                        except (KeyError, ValueError, IndexError) as e:
                            rejected.append(dict(source=source, object_id=r['object_id'], error=str(e)))
                    staging = record.with_suffix('.tmp')
                    staging.write_text(json.dumps(dict(rows=built, failures=rejected, native_decoded=native_decoded), sort_keys=True), encoding='utf-8')
                    staging.replace(record)
                    output.extend(built); failures.extend(rejected); progress['failures'] += len(rejected); progress['decoded'] += len(built)
                    progress['native_decoded'] += native_decoded
                    progress['rejected_rows'] += len(rejected)
            except (OSError, ValueError, RuntimeError, KeyError) as e:
                failures.append(dict(source=source, error=str(e))); progress['failures'] += 1
                progress['source_failed_rows'] += len(groups[source])
            finally:
                progress['done'] += 1
                snapshot = dict(format=FORMAT, status='PARTIAL_OFFLINE', counts=dict(progress),
                                total_sources=len(sources),
                                last_source=source, runtime_join_ready=False)
                staging = checkpoint_path.with_suffix('.tmp')
                staging.write_text(json.dumps(snapshot, sort_keys=True), encoding='utf-8')
                staging.replace(checkpoint_path)
    finally:
        stop.set(); thread.join(); report()
    return dict(format=FORMAT, runtime_join_ready=False,
                qualification='candidate only; exact geometry/basis/current texture views required',
                rows=output, failures=failures, counts=dict(progress, rows=len(output), total_sources=total_sources,
                    source_offset=start_source, selected_sources=len(sources), joined=0,
                    unprocessed=sum(len(v) for k, v in groups.items() if k not in selected_sources)),
                generated_locally=True, redistributable=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--plan-only', action='store_true', help='NEW inventory JSON for every matching catalogue row; no decoder required')
    ap.add_argument('--catalogue', required=True, type=Path)
    ap.add_argument('--cache', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--game', default=game_data())
    ap.add_argument('--lslib', default=lslib_tools())
    ap.add_argument('--pwsh', default=shutil.which('pwsh'))
    ap.add_argument('--limit', type=int, default=0, help='GR2 sources per incremental shard, 0 means all')
    ap.add_argument('--shard-mb', type=int, default=480, help='stream tables below 512 MB loader cap; manifest published last')
    ap.add_argument('--start-source', type=int, default=0, help='offset in deterministic lava-first source order')
    ap.add_argument('--match', nargs='+', default=['lava', 'tileset', 'hlod'])
    ap.add_argument('--batch-sources', type=int, default=0,
                    help='bounded incremental batches; out is an offline summary, not a runtime manifest')
    args = ap.parse_args()
    if os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    if args.limit < 0 or args.start_source < 0 or args.batch_sources < 0 or not 1 <= args.shard_mb <= 480:
        ap.error('source offset/limit must be nonnegative and shard-mb must be in [1,480]')
    if args.game:
        install = Path(args.game).resolve().parent
        if any(p.resolve().is_relative_to(install) for p in (args.out, args.cache, args.out.with_suffix('.manifest'))):
            ap.error('outputs/cache must be outside game install')
    if args.plan_only:
        doc = json.loads(args.catalogue.read_text(encoding='utf-8'))
        result = catalogue_plan(doc, args.match)
        write_new(args.out, result)
        say('[mesh paks] inventory ' + json.dumps(result['counts'], sort_keys=True))
        return 0
    if not args.game or not args.lslib or not args.pwsh:
        ap.error('require installed game Data, LSLib v1.20.4 Tools, and PowerShell 7 (--game/--lslib/--pwsh)')
    if not (Path(args.lslib)/'granny2.dll').is_file():
        ap.error('LSLib Tools must include granny2.dll')
    companion = args.out.with_suffix('.manifest')
    if args.out.exists() or companion.exists():
        ap.error('output and binary companion must be NEW files')
    install = Path(args.game).resolve().parent
    if any(p.resolve().is_relative_to(install) for p in (args.out, args.cache, companion)):
        ap.error('outputs/cache must be outside game install')
    doc = json.loads(args.catalogue.read_text(encoding='utf-8'))
    if args.batch_sources:
        return batch_catalogue(args, doc)
    result = build(doc, PackageSet(args.game), args.lslib, args.cache, args.pwsh, args.limit, args.match, args.start_source)
    result['binary_shards'] = write_sharded_tables(result['rows'], args.out, args.shard_mb * 1024 * 1024)
    write_new(args.out, result)
    with companion.open('x', encoding='utf-8', newline='\n') as manifest:
        manifest.write('\n'.join(result['binary_shards']) + '\n')  # complete-set marker last
    say(f'[mesh paks] wrote {args.out}, {len(result["binary_shards"])} shards; {len(result["rows"])} candidates, {len(result["failures"])} failures')
    return 0 if result['rows'] else 1


def batch_catalogue(args, doc):
    """Keep only one source batch in RAM; publish immutable per-batch manifests.

    A restart accepts only batches with a complete matching manifest and shards.
    The summary is deliberately not a runtime manifest: the full catalogue may
    exceed the runtime aggregate cap and does not prove any current resource join.
    """
    total = len({r['source'] for r in select_rows(doc, args.match)})
    end = min(total, args.start_source + args.limit) if args.limit else total
    ps = PackageSet(args.game)
    counts = Counter(); errors = Counter(); batches = []; color_rows = 0
    started = time.monotonic()
    identity = hashlib.sha256(args.catalogue.read_bytes() + Path(__file__).read_bytes()
        + Path(__file__).with_name('mesh_paks_dump.ps1').read_bytes()
        + Path(__file__).with_name('mesh_paks_dump.cs').read_bytes()
        + (Path(args.lslib)/'LSLib.dll').read_bytes() + (Path(args.lslib)/'granny2.dll').read_bytes()
        + json.dumps(ps.fingerprint(), sort_keys=True).encode()).hexdigest()
    for offset in range(args.start_source, end, args.batch_sources):
        target = args.out.with_name(args.out.stem + f'_s{offset:06d}.json')
        marker = target.with_suffix('.manifest')
        size = min(args.batch_sources, end-offset)
        if target.exists() and marker.exists():
            result = json.loads(target.read_text(encoding='utf-8'))
            if result.get('batch_identity') != identity or result['counts']['selected_sources'] != size:
                raise ValueError('completed batch belongs to a different input/decoder contract')
            names = marker.read_text().splitlines()
            if names != result['binary_shards']:
                raise ValueError('incomplete batch manifest')
            for name in names:
                path = target.parent/name
                with path.open('rb') as stream:
                    if stream.read(12) != struct.pack('<8sI', b'B3PAKS1\0', 3):
                        raise ValueError('invalid completed batch shard')
                if path.stat().st_size != result['shard_sizes'][name]:
                    raise ValueError('completed batch shard size changed')
            say(f'[mesh paks] resume completed batch {offset}/{total}')
        else:
            if target.exists() or marker.exists():
                raise ValueError('incomplete publication: use a NEW out prefix; per-GR2 cache is reusable')
            decoder = Decoder(args.lslib, args.pwsh)
            try:
                result = build(doc, ps, args.lslib, args.cache, args.pwsh, size,
                               args.match, offset, decoder)
            finally:
                decoder.close()
            result['batch_identity'] = identity
            result['binary_shards'] = write_sharded_tables(result['rows'], target, args.shard_mb*1024*1024)
            result['shard_sizes'] = {n: (target.parent/n).stat().st_size for n in result['binary_shards']}
            write_new(target, result)
            with marker.open('x', encoding='utf-8') as stream:
                stream.write('\n'.join(result['binary_shards']) + '\n')
        for key in ('done', 'decoded', 'failures', 'hits', 'candidates', 'native_decoded', 'rejected_rows', 'source_failed_rows'):
            counts[key] += result['counts'][key]
        color_rows += sum(r.get('color_layout') == 1 for r in result['rows'])
        errors.update(f['error'] for f in result['failures'])
        batches.append(target.name)
        elapsed = time.monotonic()-started
        say(f'[mesh paks] TOTAL {counts["done"]}/{end-args.start_source} GR2; decoded {counts["decoded"]}, '
            f'failures {counts["failures"]}; elapsed {elapsed:.1f}s; immutable batch {target.name}')
    plan = catalogue_plan(doc, args.match)
    summary = dict(format=FORMAT, status='PARTIAL_OFFLINE', batch_identity=identity,
        counts=dict(counts, joined=0, color_rows=color_rows, total_sources=total,
                    unprocessed_sources=total-counts['done'], rejected_metadata=plan['counts']['rejected_metadata'],
                    unprocessed=plan['counts']['eligible']-counts['candidates']),
        failure_modes=dict(errors), batches=batches, generated_locally=True, redistributable=False,
        qualification='decoded identity candidates only; no replay or runtime joins proved')
    write_new(args.out, summary)
    return 0 if counts['decoded'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
