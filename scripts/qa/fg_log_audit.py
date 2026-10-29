"""Read retained logs only: DLSS-G transitions and existing GPU timings."""
import argparse
import json
from pathlib import Path
import re


def time_of(line):
    m=re.match(r'(\d\d):(\d\d):(\d\d)\.(\d{3})',line)
    return sum(int(v)*scale for v,scale in zip(m.groups(),(3600,60,1,.001))) if m else None


def audit(text):
    decisions=[]; transitions=[]; destroys=[]; inputs={}; gpu=[]; sdk=[]; disabled=[]
    previous=None; pending_destroys=[]; last_tags=None; readiness=None
    for number,line in enumerate(text.splitlines(),1):
        t=time_of(line)
        if t is None: continue
        row={'line':number,'time':line[:12],'text':line}
        match=re.search(r'readiness frame (\d+): requested (\d+) last option (-?\d+) host running \d+ ready (\d+) stable (\d+) gate (0x[0-9a-f]+)',line)
        if match:
            frame,requested,applied,ready,stable,gate=match.groups()
            value=int(gate,16)
            labels={1:'no token',2:'depth missing',4:'MV missing',8:'hudless missing',16:'UI missing',
                    32:'MV stale',64:'UI stale',128:'recent-destruction cooldown',256:'stability warmup'}
            readiness={**row,'frame':int(frame),'ready':bool(int(ready)),'stable':bool(int(stable)),
                       'gate':value,'reasons':[name for bit,name in labels.items() if value&bit]}
        match=re.search(r'DLSS-G disabled: (.*)',line)
        if match: disabled.append({**row,'reason':match[1]})
        match=re.search(r'DLSS-G interpolation state changed from (enabled|disabled) to (enabled|disabled).*mode=sl::DLSSGMode::(eOn|eOff)',line)
        if match:
            before,after,mode=match.groups()
            # Same-timestamp SDK reason is direct evidence; do not associate an
            # unrelated focus message from an earlier part of the session.
            reason=disabled[-1]['reason'] if disabled and disabled[-1]['time']==row['time'] else 'UNKNOWN'
            if mode=='eOff' and after=='disabled': reason='host requested off'
            sdk.append({**row,'seconds':t,'before':before,'after':after,'mode':mode,'reason':reason})
        if '[sl] a tagged image was destroyed;' in line:
            destroys.append(row); pending_destroys.append(row)
        match=re.search(r'\[sl\] (hudless|UI color\+alpha) = image (\S+)',line)
        if match: inputs[match[1]]={'image':match[2],**row}
        if re.search(r'\[sl\] frame \d+: tags ',line): last_tags=row
        match=re.search(r'slDLSSGSetOptions\(mode (on|off),.*flags (0x[0-9a-f]+)\): (\S+)',line)
        if match:
            mode,flags,result=match.groups()
            record={**row,'seconds':t,'mode':mode,'flags':flags,'result':result,
                'transition':previous is not None and mode!=previous,
                'initial':previous is None,'destroy_events_since_previous_options':len(pending_destroys),
                'last_destroy':pending_destroys[-1] if pending_destroys else None,
                'last_identified_inputs':dict(inputs),'last_periodic_tag_API':last_tags,
                'invalid_input':'UNKNOWN: legacy logs lack destroyed image identity/current role and per-toggle input state'}
            if readiness and readiness['time']==row['time']:
                record['readiness']=readiness
                record['invalid_input']=(', '.join(readiness['reasons']) if not readiness['ready'] else
                    'NONE missing/stale: current inputs ready; '+(', '.join(readiness['reasons']) or 'stable'))
            decisions.append(record)
            if record['transition']: transitions.append(record)
            previous=mode; pending_destroys=[]
        if re.search(r'rt passes: GPU cost|gi: trace pass .*GPU median|ls: local-light shadow pass GPU|rf: reflection pass GPU|\[ngx\] gpu RR:|\[rrpost\] gpu:',line):
            gpu.append(row)
    peak=max((sum(start['seconds']<=r['seconds']<start['seconds']+60 for r in transitions) for start in transitions),default=0)
    off=[r for r in transitions if r['mode']=='off']
    return {'options_calls':len(decisions),'mode_transitions':len(transitions),'peak_transitions_in_60_seconds':peak,
        'off_transitions':len(off),'off_preceded_by_destroy_since_previous_options':sum(bool(r['last_destroy']) for r in off),
        'destroy_events':len(destroys),'decisions':decisions,'gpu_lines':gpu,
        'sdk_transitions':sdk,'sdk_disabled_reasons':disabled,
        'limits':'Association is not identification of an invalid input. Flags-only calls are not toggles. No counterfactual fix result is inferred.'}


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--logs',type=Path,nargs='+',required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    reports={str(path):audit(path.read_text(errors='replace')) for path in a.logs}
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(reports,indent=2)+'\n')
    for name,r in reports.items():
        print(name,':',r['mode_transitions'],'toggles, peak',r['peak_transitions_in_60_seconds'],
            '/60s, off preceded by destroy',r['off_preceded_by_destroy_since_previous_options'],'/',r['off_transitions'])


if __name__=='__main__': main()
