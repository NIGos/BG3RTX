"""Read-only animation census of the complete material catalogue and installed shaders.

Counts are non-exclusive capabilities, not a claim that an instance's switches are on.
Every template is scanned, including unnamed/unclassified time users. No saves are read.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import time

from . import lsf
from .locate import game_data
from .lspk import PackageSet
from .mesh_materials import write_new

RULES = {
    'scroll': r'scroll|pann|(?:uv|texture).*speed|speed.*(?:uv|texture)',
    'flowmap': r'flow.?map|flow.?noise|flow.?speed',
    'sprite': r'flipbook|sprite|sub.?image|frame.?rate|frames.?per',
    'wind_cloth': r'wind|cloth|flutter',
    'pulse': r'pulse|pulsat',
    'displacement': r'displac|bounce|wpo|height.*(?:anim|speed)|(?:anim|speed).*height',
    'water': r'fluid.water',
}


def classes(text):
    return [k for k, v in RULES.items() if re.search(v, text, re.I)]


def compact_counts(path, report):
    """Read the bounded v4 metadata, without decoding or modifying any geometry."""
    data = Path(path).read_bytes()
    if len(data) < 16 or len(data) > 512*1024*1024:
        raise ValueError('compact size')
    magic, version, n = struct.unpack_from('<8sII', data)
    if magic != b'B3PAKS1\0' or version != 4 or n > 1000000:
        raise ValueError('expected compact v4')
    at, mids = 16, []
    for _ in range(n):
        if at+912 > len(data):
            raise ValueError('truncated compact row')
        mids.append(data[at+804:at+840].decode('ascii'))
        refs = struct.unpack_from('<I', data, at+908)[0]; at += 912
        if refs > 128:
            raise ValueError('texture ref count')
        for _ in range(refs):
            if at+4 > len(data):
                raise ValueError('truncated texture ref')
            length = struct.unpack_from('<I', data, at)[0]
            if not 0 < length <= 128 or at+4+length+36 > len(data):
                raise ValueError('texture ref size')
            at += 4+length+36
    if at != len(data):
        raise ValueError('compact trailing bytes')
    m = report['materials']; unique = set(mids)
    return {'sha256': hashlib.sha256(data).hexdigest(), 'rows': n, 'instances': len(unique),
            'classes': {c: {'rows': sum(c in m.get(x, {}).get('classes', []) for x in mids),
                             'instances': sum(c in m.get(x, {}).get('classes', []) for x in unique),
                             'templates': len({m[x]['template'] for x in unique if c in m.get(x, {}).get('classes', [])})} for c in RULES},
            'unresolved_ids': sorted(unique-set(m))}


def time_reads(raw):
    """Actual OpLoad through a 0:0 member-zero access, not merely a declared UBO.

    Engine global time is a scalar at std140 member 0, byte 0. Return stages
    that read it. Does not claim that every such load reaches an output.
    """
    stages = []
    at = 0
    while True:
        at = raw.find(b'SHD8', at)
        if at < 0:
            break
        size = struct.unpack_from('<I', raw, at + 10)[0]
        spv = raw[at + 50:at + 50 + size]; at += 50 + size
        if len(spv) < 20 or len(spv) % 4 or spv[:4] != b'\x03\x02\x23\x07':
            continue
        w = struct.unpack('<%dI' % (len(spv)//4), spv)
        ins, i = [], 5
        while i < len(w):
            n, op = w[i] >> 16, w[i] & 65535
            if not n or i+n > len(w):
                raise ValueError('malformed SPIR-V')
            ins.append((op, w[i+1:i+n])); i += n
        deco, constants, pointers, stage = defaultdict(dict), {}, {}, None
        for op, a in ins:
            if op == 71 and len(a) >= 3:
                deco[a[0]][a[1]] = a[2]
            if op == 43 and len(a) == 3:
                constants[a[1]] = a[2]
            if op == 15:
                stage = {0: 'VS', 4: 'FS'}.get(a[0], str(a[0]))
        roots = {v for v, d in deco.items() if d.get(34) == 0 and d.get(33) == 0}
        read = False
        for op, a in ins:
            if op in (65, 66) and len(a) >= 4:
                root, path = pointers.get(a[2], (a[2], ()))
                pointers[a[1]] = root, path + tuple(constants.get(x, -1) for x in a[3:])
            if op == 61:
                root, path = pointers.get(a[2], (None, ()))
                if root in roots and path == (0,):
                    read = True
        if read:
            stages.append(stage)
    return sorted(set(stages))


def build(doc, ps, cache_dir):
    cache_dir.mkdir(parents=True, exist_ok=True)
    visual_materials = defaultdict(set)
    for r in doc['rows']:
        if r['lod'] == 0 and r['material_resolved'] and not r['ambiguous']:
            visual_materials[r['visual_id']].add(r['material_id'])
    templates = {}; started = last = time.monotonic(); n = len(doc['templates'])
    for i, (path, cat) in enumerate(sorted(doc['templates'].items())):
        if time.monotonic()-last > 25:
            rate = i / max(time.monotonic()-started, .001)
            print(f'[animation] {i}/{n} templates, {rate:.1f}/s, ETA {(n-i)/max(rate,.001):.0f}s, '
                  f'failures {sum(bool(x.get("error")) for x in templates.values())}', flush=True)
            last = time.monotonic()
        try:
            if path not in ps:
                templates[path] = {'error': 'missing template'}; continue
            raw = ps.read(path); digest = hashlib.sha256(raw).hexdigest()
            cache = cache_dir / (digest + '.json')
            # Cache facts by template AND shader bytes: package patches cannot reuse stale time facts.
            params = [x.to_dict(2) for r in lsf.parse(raw).regions for x in r.children if x.get('ParameterName')]
            evidence = []
            for inst in ('ST', 'STI'):
                for kind in ('DEF', 'FOR', 'EMI', 'DEP', 'VEL'):
                    name = path[:-4] + f'_{inst}_{kind}_Vulkan.bshd'
                    if name not in ps:
                        continue
                    shader = ps.read(name); sha = hashlib.sha256(shader).hexdigest()
                    sc = cache_dir / (sha + '.time.json')
                    if sc.exists():
                        stages = json.loads(sc.read_text())
                    else:
                        stages = time_reads(shader); sc.write_text(json.dumps(stages), encoding='utf-8')
                    if stages:
                        evidence.append({'path': name, 'sha256': sha, 'stages': stages})
            tags = classes(path + ' ' + ' '.join(x['ParameterName'] for x in params))
            templates[path] = {'sha256': digest, 'classes': tags if evidence else [],
                               'parameter_candidates': tags, 'time_reads': evidence, 'parameters': params}
            cache.write_text(json.dumps(templates[path]), encoding='utf-8')
        except (ValueError, KeyError, IndexError) as ex:
            templates[path] = {'error': str(ex)}
    materials = {}
    for mid, mat in doc['materials'].items():
        t = templates.get(mat['source'], {})
        tags = t.get('classes', [])
        materials[mid] = {'template': mat['source'], 'classes': tags,
                          'time_dependent_template': bool(t.get('time_reads')),
                          'ambiguous': 'MaterialBank:'+mid in doc['conflicts'],
                          'overrides': [x for x in mat['native'].get('children', [])
                                        if t.get('time_reads') and x.get('Enabled') and x.get('ParameterName')
                                        and x.get('_') in ('ScalarParameters', 'Vector2Parameters', 'Vector3Parameters', 'Vector4Parameters')]}
    def summary(ids):
        ids = set(ids); paths = {materials[m]['template'] for m in ids if m in materials}
        return {'materials': len(ids), 'templates': len(paths),
                'classes': {c: {'templates': sum(c in templates.get(p, {}).get('classes', []) for p in paths),
                                'instances': sum(c in materials.get(m, {}).get('classes', []) for m in ids)} for c in RULES},
                'time_templates': sum(bool(templates.get(p, {}).get('time_reads')) for p in paths),
                'time_instances': sum(materials.get(m, {}).get('time_dependent_template', False) for m in ids)}
    # The catalogue has no placements. Use the existing level/root-template resolver;
    # counts are distinct MaterialBank IDs reached from LOD 0 Scenery/Items, not pixels.
    from .extract.world import LevelIndex
    from tools.material_families.world import Templates
    li, roots = LevelIndex(ps), Templates(ps)
    all_levels = set(li.files) | set(li.globals)
    scopes = {'camp': ['WLD_Campfire_E', 'CMP_Shrubbery_A', 'CMP_Shrubbery_B'],
              'S6': ['WLD_Underdark_C', 'WLD_UnderdarkSubs_C', 'PLT_UND_AdamantineForge', 'PLT_UND_AdamantineForgeAnvil'],
              'S8': ['BGH_WaterQueenHouse_B_ART'],
              'inn': ['BGH_BlushingMermaid_A', 'BGH_BlushingMermaidCellarB']}
    levels = {}
    for scene, scope in scopes.items():
        mids, unresolved, objects = set(), 0, 0
        for lv in scope:
            for cat in ('Scenery', 'Items'):
                for glob in (False, True):
                    for obj in li.objects(lv, cat, glob):
                        objects += 1
                        vid = roots.visual(obj)
                        rows = visual_materials.get(vid, ())
                        if not rows:
                            unresolved += 1
                        mids.update(rows)
        levels[scene] = {**summary(mids), 'levels': scope, 'objects': objects, 'unresolved_visual_objects': unresolved}
        print(f'[animation] level {scene}: {objects} objects, {len(mids)} material IDs', flush=True)
    return {'format': 'BG3RTX_ANIMATION_CENSUS_V1', 'scope': 'all catalogue templates; capabilities overlap; switches/zero speeds may disable an instance',
            'time_source': 'set 0 binding 0 std140 scalar member 0 byte 0; actual SPIR-V OpLoad',
            'counts': summary(materials), 'levels': levels, 'templates': templates, 'materials': materials,
            'unclassified_time_templates': [p for p, t in templates.items() if t.get('time_reads') and not t['classes']],
            'errors': {p: t['error'] for p, t in templates.items() if t.get('error')}}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--catalogue', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--cache', required=True, type=Path)
    ap.add_argument('--compact', type=Path)
    args = ap.parse_args()
    if args.out.exists():
        ap.error('output exists; choose a new path')
    from tools.low_priority import lower_priority
    lower_priority()
    doc = json.loads(args.catalogue.read_text(encoding='utf-8'))
    report = build(doc, PackageSet(game_data()), args.cache)
    if args.compact:
        report['compact'] = compact_counts(args.compact, report)
    report['catalogue_sha256'] = hashlib.sha256(args.catalogue.read_bytes()).hexdigest()
    write_new(args.out, report)
    print(json.dumps({'counts': report['counts'], 'levels': report['levels'], 'errors': report['errors']}), flush=True)


if __name__ == '__main__':
    main()
