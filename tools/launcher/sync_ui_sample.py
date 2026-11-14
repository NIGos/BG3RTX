"""Rewrites the sample rows of tools/launcher/ui/app.js from the settings table, after a copy change:
    py -3 tools/launcher/sync_ui_sample.py <full path>/bin/launcher_web_bridge_test.exe
Title, page, choice labels and description come from the bridge (--json experimental); each sample value is kept.
Cost, detail text and preset values go to `sampleMeta`, so the plain-browser preview shows the real chips and details."""
import json,re,subprocess,sys
real={s['id']:s for s in json.loads(subprocess.check_output([sys.argv[1],'--json','experimental'],encoding='utf-8'))['settings']}
p='tools/launcher/ui/app.js'; t=open(p,encoding='utf-8').read()
a=t.index(' const settings=['); b=t.index("].filter(x=>x[2]!=='Experimental'",a)
def q(text):
    return "'"+text.replace('\\','\\\\')+"'" if "'" not in text else '"'+text.replace('\\','\\\\').replace('"','\\"')+'"'
rows=[]
for line in t[a:b].split('\n')[1:]:
    line=line.strip().rstrip(',')
    if not line: continue
    m=re.match(r"\['([a-z0-9_]+)',.*\],(-?\d+),",line); assert m,line[:80]
    s=real[m.group(1)]; value=int(m.group(2))
    if s['choices']: value=min(value,len(s['choices'])-1)
    rows.append(" ['%s',%s,'%s',[%s],%d,%s]"%(s['id'],q(s['title']),s['category'],','.join(q(c['label']) for c in s['choices']),value,q(s['description'])))
assert len(rows)==len(real),(len(rows),len(real))
meta={i:{k:r[k] for k in ('cost','vram','provenance','details','presets')} for i,r in real.items()}
line=' const sampleMeta='+json.dumps(meta,ensure_ascii=False,separators=(',',':'))+';\n'
start=t.rfind(' const sampleMeta=',0,a)
if start>=0: t=t[:start]+t[a:]; a=start; b=t.index("].filter(x=>x[2]!=='Experimental'",a)
t=t[:a]+line+' const settings=[\n'+',\n'.join(rows)+'\n '+t[b:]
open(p,'w',encoding='utf-8',newline='\n').write(t)
print('sample rows synced:',len(rows))
