"""Retained-frame Tileset draw inventory. No save reads, writes or game launch."""
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qa import coverage as c


def main():
    c._FOLDERS = []  # Disable coverage.py's optional save-folder association fallback.
    db = c.Db(c.MAIN / 'data/assetdb/bg3rtx_assetdb.bin')
    if not db.ok:
        raise ValueError('material DB required')
    smap, ids = c.scene_map(), c.scene_ids()
    caps = c.find_captures()
    best = {}
    start = time.monotonic()
    for i, cap in enumerate(caps):
        frame = c.load_frame(cap)
        if not frame:
            continue
        counts, depth = {}, 0
        for p in c.passes_of(frame):
            for fs, vs, n in p['draws']:
                if len(p['color']) == 5 and 'tileset' in db.name(fs).lower():
                    counts[fs] = counts.get(fs, 0) + n
                if fs in ('781cd8d1d600117d', 'ca4bf1d78f6438fe') and not p['color']:
                    depth += n
        scene = c.scene_of(cap, smap, ids)
        total = sum(counts.values())
        if total > best.get(scene, {}).get('draws', 0):
            best[scene] = dict(draws=total, capture=str(cap), fs_counts=counts,
                               supported_depth_draws=depth,
                               supported_material_draws=counts.get('cad41eff979970e4', 0))
        if i % 100 == 0:
            print(f'{i}/{len(caps)} captures, {time.monotonic()-start:.1f}s', flush=True)
    out = dict(captures_scanned=len(caps), scenes=best,
               method='Maximum Tileset-named G-buffer draws in one retained frame per identified save. DB names, not all-pass counts. Unknown saves excluded from choice. Supported exact shader pairs counted separately.')
    Path(__file__).with_name('scene_inventory.json').write_text(json.dumps(out, indent=2) + '\n')


if __name__ == '__main__':
    main()
