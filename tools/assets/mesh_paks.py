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

import numpy as np

from .locate import game_data, lslib_tools
from .lspk import PackageSet
from .mesh_materials import write_new
from ..hair_groom.asset import content_id

FORMAT = 'BG3RTX_MESH_PAKS_V1'
VERSION = 'mesh-paks-1'
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

    if take(4) != b'GR2P' or u32() != 1:
        raise ValueError('unsupported GR2P version')
    result = {}
    for _ in range(u32()):
        name = take(u32()).decode('utf-8')
        nv, ni, ng = u32(), u32(), u32()
        verts = np.frombuffer(take(nv * 20), '<f4').reshape(nv, 5)
        idx = np.frombuffer(take(ni * 4), '<u4')
        groups = np.frombuffer(take(ng * 12), '<u4').reshape(ng, 3)
        if name in result:
            raise ValueError('duplicate native mesh name')
        if ni % 3 or not ni or idx.max() >= nv or not np.isfinite(verts).all():
            raise ValueError('unsupported/non-finite geometry')
        for _, first, count in groups:
            if not count or int(first) + int(count) > ni // 3:
                raise ValueError('invalid native triangle group')
        result[name] = (verts, idx, groups)
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


def binary_table(rows):
    # v2 appends full native ordered position/UV corners to each candidate.
    data = bytearray(struct.pack('<8sII', b'B3PAKS1\0', 2, len(rows)))
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
    return bytes(data)


def build(doc, ps, lslib, cache, pwsh='pwsh', limit=0, match=('lava', 'tileset', 'hlod')):
    cache = Path(cache); cache.mkdir(parents=True, exist_ok=True)
    groups = {}
    for row in select_rows(doc, match):
        groups.setdefault(row['source'], []).append(row)
    sources = sorted(groups, key=lambda s: (not any('lava' in doc['materials'][r['material_id']]['source'].lower() for r in groups[s]), s))
    if limit:
        sources = sources[:limit]
    progress = dict(done=0, failures=0, hits=0)
    start = time.monotonic(); stop = threading.Event()

    def report():
        elapsed = max(time.monotonic()-start, 1e-9)
        rate = progress['done']/elapsed
        say(f"[mesh paks] {progress['done']}/{len(sources)} GR2, {rate:.2f}/s, "
            f"ETA {(len(sources)-progress['done'])/max(rate,1e-9):.0f}s, failures {progress['failures']}, cached {progress['hits']}")

    def heartbeat():
        while not stop.wait(25):
            report()

    thread = threading.Thread(target=heartbeat, daemon=True); thread.start()
    output, failures = [], []
    decoder_identity = hashlib.sha256(Path(__file__).read_bytes()).digest()
    for dependency in ('LSLib.dll', 'granny2.dll'):
        dep = Path(lslib)/dependency
        if dep.is_file():
            decoder_identity += hashlib.sha256(dep.read_bytes()).digest()
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
                        binary_table(saved['rows'])  # a missing/truncated full stream invalidates the cache
                    except (OSError, ValueError, KeyError, struct.error):
                        saved = None
                    if saved is not None:
                        output.extend(saved['rows']); failures.extend(saved['failures'])
                        progress['failures'] += len(saved['failures']); progress['hits'] += 1
                        continue
                with tempfile.TemporaryDirectory(prefix='bg3-paks-') as tmp:
                    src, decoded = Path(tmp)/'input.gr2', Path(tmp)/'decoded.bin'
                    src.write_bytes(raw); dump(lslib, src, decoded, pwsh)
                    meshes = read_dump(decoded)
                    built, rejected = [], []
                    for r in relevant:
                        try:
                            name, export_order = mesh_object(r['object_id'])
                            verts, idx, native_groups = meshes[name]
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
                            built.append(dict(**sig, **material_info(doc, r['material_id']), source=source,
                                              object_id=r['object_id'], lod=r['lod'],
                                              export_order=export_order,
                                              identity='source + unique native mesh name; single full-range material group',
                                              native_export_order_verified=False, native_lod_verified=False,
                                              full_geometry_cache=str(full_path.resolve())))
                        except (KeyError, ValueError, IndexError) as e:
                            rejected.append(dict(source=source, object_id=r['object_id'], error=str(e)))
                    staging = record.with_suffix('.tmp')
                    staging.write_text(json.dumps(dict(rows=built, failures=rejected), sort_keys=True), encoding='utf-8')
                    staging.replace(record)
                    output.extend(built); failures.extend(rejected); progress['failures'] += len(rejected)
            except (OSError, ValueError, RuntimeError, KeyError) as e:
                failures.append(dict(source=source, error=str(e))); progress['failures'] += 1
            finally:
                progress['done'] += 1
    finally:
        stop.set(); thread.join(); report()
    return dict(format=FORMAT, runtime_join_ready=False,
                qualification='candidate only; exact geometry/basis/current texture views required',
                rows=output, failures=failures, counts=dict(progress, rows=len(output)),
                generated_locally=True, redistributable=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--catalogue', required=True, type=Path)
    ap.add_argument('--cache', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--game', default=game_data())
    ap.add_argument('--lslib', default=lslib_tools())
    ap.add_argument('--pwsh', default=shutil.which('pwsh'))
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--match', nargs='+', default=['lava', 'tileset', 'hlod'])
    args = ap.parse_args()
    if os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    if not args.game or not args.lslib or not args.pwsh:
        ap.error('require installed game Data, LSLib v1.20.4 Tools, and PowerShell 7 (--game/--lslib/--pwsh)')
    if not (Path(args.lslib)/'granny2.dll').is_file():
        ap.error('LSLib Tools must include granny2.dll')
    companion = args.out.with_suffix('.bin')
    if args.out.exists() or companion.exists():
        ap.error('output and binary companion must be NEW files')
    install = Path(args.game).resolve().parent
    if any(p.resolve().is_relative_to(install) for p in (args.out, args.cache, companion)):
        ap.error('outputs/cache must be outside game install')
    doc = json.loads(args.catalogue.read_text(encoding='utf-8'))
    result = build(doc, PackageSet(args.game), args.lslib, args.cache, args.pwsh, args.limit, args.match)
    binary = binary_table(result['rows'])
    write_new(args.out, result)
    with companion.open('xb') as f:
        f.write(binary)
    say(f'[mesh paks] wrote {args.out}, {companion}; {len(result["rows"])} candidates, {len(result["failures"])} failures')
    return 0 if result['rows'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
