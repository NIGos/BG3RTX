"""Authored lights spatially relevant to the fixed S1 open bay; never live activation.

Reuses the existing world/template/light extractor. Broad sphere policy is the
extractor's authored support envelope, not native visibility/occlusion proof.
"""
import argparse
import csv
from decimal import Decimal
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
from .lspk import PackageSet
from .build import find_data_dir,DEFAULT_GAME
from .affine import Affine
from .extract.world import LevelIndex
from .extract.lights import TemplateResolver,extract,resolve,DEFAULTS,TYPE_NAMES,light_bounds_radius


def sphere_box_candidate(position,radius,bounds):
    values=(*position,radius,*bounds[0],*bounds[1])
    if len(position)!=3 or len(bounds[0])!=3 or len(bounds[1])!=3 or not all(math.isfinite(v) for v in values) or radius<0 or any(a>b for a,b in zip(*bounds)):
        raise ValueError('Invalid authored support sphere/cell bounds')
    distance2=math.fsum(max(bounds[0][j]-position[j],0,position[j]-bounds[1][j])**2 for j in range(3))
    return distance2<=radius*radius,math.sqrt(distance2)


def world_affines(li,world):
    result=[];chain=[]
    for inst in world.instances:
        if inst.parent<0:result.append(Affine());chain.append([]);continue
        parent=world.instances[inst.parent];candidates=[]
        for n in li.objects(parent.level,'LevelTemplates'):
            template=li._level_templates.get(n.get('TemplateName'))
            if template is not None and template.get('SubLevelName')==inst.level and (n.get('Name') or inst.level)==inst.name:candidates.append(n)
        if len(candidates)!=1:raise ValueError('Ambiguous original native level placement')
        n=candidates[0];result.append(result[inst.parent].compose(Affine.from_node(n.child('Transform'))));chain.append(chain[inst.parent]+[n.to_dict()])
    return result,chain


def historical_rows(files,lights):
    """Keep existing producer's join identities, never reinterpret them as current."""
    rows=[];names={l['resolved']['name'] for l in lights}
    for path in files:
        for row in csv.DictReader(path.read_text(encoding='utf-8-sig').splitlines()):
            if row.get('db_name') in names:
                candidates=[]
                for light in lights:
                    if light['resolved']['name']!=row['db_name']:continue
                    xf=Affine(**light['full_affine']);error=xf.fp32_application_bound((0,0,0))
                    native=[float(row[k]) for k in ('x','y','z')]
                    rounding=[float(Decimal(1).scaleb(Decimal(row[k]).as_tuple().exponent))/2 for k in ('x','y','z')]
                    if all(abs(a-b)<=e+q for a,b,e,q in zip(native,xf.translation,error,rounding)):
                        candidates.append(light['resolved']['map_key'])
                rows.append(dict(file=str(path),row=row,position_and_name_candidates=candidates,
                    scope='HISTORICAL_ONLY_NOT_SAME_FRAME_OR_UUID_PROOF'))
    return rows


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--assets',type=Path,default=Path('captures/m2-foundry'))
    ap.add_argument('--join-dir',type=Path,required=True);args=ap.parse_args();base=args.assets.resolve();out=base/'cell-lights';out.mkdir(exist_ok=True)
    evidence_path=base/'reference-cell-archive-terrain/evidence.json';evidence=json.loads(evidence_path.read_text());bounds=evidence['selection_bounds']
    ps=PackageSet(find_data_dir(DEFAULT_GAME));li=LevelIndex(ps);world=next(w for w in li.worlds() if w.name=='CTY_Main_A')
    resolver=TemplateResolver(ps);lights,stats=extract(li,[world],resolver);affines,chains=world_affines(li,world)
    selected=[];sources={}
    def source(path):
        if path not in sources:
            raw=ps.read(path);sources[path]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),package=ps.owner(path))
    for light in lights:
        radius=light_bounds_radius(light);hit,distance=sphere_box_candidate(light.pos,radius,bounds)
        if not hit:continue
        inst=world.instances[light.instance];category='CombinedLights' if light.flags&(1<<11) else 'Lights'
        raw=next(n for n in li.objects(inst.level,category) if n.get('MapKey')==light.map_key)
        xf=affines[light.instance].compose(Affine.from_node(raw.child('Transform')))
        raw_fields={field:dict(value=resolve(raw,resolver,field)[0],authored_or_inherited=resolve(raw,resolver,field)[1]) for field in DEFAULTS}
        record=dict(resolved=asdict(light),type_name=TYPE_NAMES[light.type],source_level=inst.level,instance_chain=chains[light.instance],raw_placement=raw.to_dict(),
            raw_template_chain=[n.to_dict() for n in resolver.chain(raw.get('TemplateName',''))],resolved_fields=raw_fields,
            full_affine=asdict(xf),affine_position_delta=[a-b for a,b in zip(xf.translation,light.pos)],support=dict(policy='EXISTING_EXTRACTOR_SPHERE_NOT_NATIVE_ACTIVATION',radius=radius,distance_to_cell=distance),
            runtime_activation='UNKNOWN',runtime_native_parameters='UNKNOWN',archive_enabled=raw_fields['Enabled']['value'])
        selected.append(record)
        for _,p in li.files[inst.level].get(category,[]):source(p)
    template_ids={n['MapKey'] for l in selected for n in l['raw_template_chain']}
    # Existing resolver intentionally owns inheritance; this pass adds provenance.
    from . import lsf
    for p in ps.iter_suffix('RootTemplates/_merged.lsf'):
        if any(n.get('MapKey') in template_ids for n in lsf.parse(ps.read(p)).find_all('GameObjects')):source(p)
    for l in selected:
        for node in l['instance_chain']:
            for p in ps.index:
                if p.endswith('/LevelTemplates/_merged.lsf') and ('/'+node.get('LevelName','!')+'/') in p:source(p)
    joins=sorted(args.join_dir.glob('assetdb_join*.csv'));rows=historical_rows(joins,selected)
    report=dict(scope='ONE authored open-bay light candidate set; no native activation or radiance claim',world=world.name,bounds=bounds,
        cell_evidence_sha256=hashlib.sha256(evidence_path.read_bytes()).hexdigest(),world_instances=len(world.instances),world_lights=len(lights),selected=selected,
        type_counts={name:sum(l['type_name']==name for l in selected) for name in TYPE_NAMES},source_fingerprints=sources,
        historical_join_files=[dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in joins],historical_name_candidates=rows,
        missing_templates=dict(resolver.missing),extractor_stats=stats,
        limits=['Existing point/spot radius and box half-diagonal+att_end support envelope reused; not exact cone/OBB clipping',
                'Candidate selection inherits existing extractor transform/radius conventions; no whole-world nonuniform-placement completeness proof',
                'Full affine/raw transform retained for selected instances; normalized quaternion is an explicit convention',
                'No native photometric/colour-temperature/exposure/cookie/shadow conversion inferred from names',
                'Archive Enabled/StartingLoaded is not current gameplay activation; historical joins are not current cell proof',
                'No emissive mesh inferred to be an actual light; no occlusion or room-closure assertion'])
    (out/'evidence.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(world_lights=len(lights),selected=len(selected),types=report['type_counts'],historical_files=len(joins),historical_name_candidates=len(rows),missing_templates=report['missing_templates']),indent=2))


if __name__=='__main__':main()
