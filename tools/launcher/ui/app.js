/* UI is a renderer. The native bridge owns policy, dependencies, edits and persistence. */
'use strict';
const $ = id => document.getElementById(id);
const query = new URLSearchParams(location.search);
const sample = query.has('sample');
const localFiles = location.protocol==='file:';
const assetBase = localFiles?'../../../assets/':'';
const fontBase = localFiles?assetBase+'fonts/':'https://fonts.bg3rtx.local/';
const fontStyle=document.createElement('style');
fontStyle.textContent='@font-face{font-family:Lato;src:url("'+fontBase+'LatoWeb-Regular.ttf")}@font-face{font-family:Lato;src:url("'+fontBase+'LatoWeb-Semibold.ttf");font-weight:600 900}';
document.head.append(fontStyle);
if(localFiles)['dark-stone.svg','frame.svg','corner.svg','divider.svg','hero-lantern.png','parchment-panel.svg'].forEach(name=>document.documentElement.style.setProperty('--art-'+name.split('.')[0], 'url("'+assetBase+'branding/ornaments/'+name+'")'));
document.querySelector('.emblem').src=localFiles?assetBase+'branding/bg3rtx.svg':'https://branding.bg3rtx.local/bg3rtx.svg';
let state = null;
const sampleView=query.get('view')||'';
let section = query.get('section') || (sampleView==='missing'?'Support':'Lighting');
let experimentalOpen = query.has('experimental')||sampleView==='experimental';
const disclosureState=new Map();
let reportSignature=null;
function rememberDisclosures(root) {root.querySelectorAll('details').forEach(d=>{if(d.id)disclosureState.set(d.id,d.open);});}
function restoreDisclosure(d) {d.open=disclosureState.get(d.id)||false;}
const sections = [['Lighting','M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2'],['Surfaces','M3 15c3-2 6 2 9 0s6-2 9 0M3 19c3-2 6 2 9 0s6-2 9 0M12 3v8'],['Performance','M3 17l5-6 4 4 4-7 5 9M3 21h18'],['Advanced','M4 8h16M8 4v16M16 4v16M4 16h16'],['Support','M12 4v8M12 17v1']];
const labels = {Surfaces:'Surfaces',Advanced:'Advanced',Support:'Help & support'};
function el(tag, className, text) { const e=document.createElement(tag);if(className)e.className=className;if(text!==undefined)e.textContent=text;return e; }
function send(type, extra={}) { if(sample) { $('status').textContent='Sample preview — actions do not change settings or launch the game';return; } window.chrome?.webview?.postMessage({type,...extra}); }
function measured(text) {return !!text && !/unknown|not measured|unavailable/i.test(text);}
function pill(text,kind) {return el('span','pill '+(kind||''),text);}
function showDialog(d) {if(!d.open)d.showModal();}
function card(s) {
 const c=el('article','card');c.id='card-'+s.id;const r=el('div','row');r.append(el('h3','',s.title||s.label));c.append(r);
 c.append(el('p','description',(s.description||s.summary||'')+(s.developer?' GPU cost: '+(s.cost||'Not measured.') : '')));
 const control=el('div','control row');
 const choices=s.choices||[];
 if(s.custom)control.append(el('span','help-copy','Custom value kept. Choose an option to replace it.'));
 if(choices.length===2&&choices[0].label==='Off'&&choices[1].label==='On') {
  const l=el('label','toggle'),i=el('input');i.type='checkbox';i.checked=s.value===choices[1].value;i.disabled=choices[i.checked?0:1].enabled===false;i.setAttribute('aria-label',s.title||s.label);i.title=[choices[i.checked?0:1].apply,choices[i.checked?0:1].reason].filter(Boolean).join('; ');i.onchange=()=>send('set',{id:s.id,value:choices[i.checked?1:0].value});l.append(i,el('span'));control.append(l,el('span','help-copy',i.checked?'On':'Off'));
 } else if(choices.length) {
  const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-label',s.title||s.label);
  choices.forEach(ch=>{const b=el('button',ch.value===s.value?'on':'',ch.label);b.disabled=ch.enabled===false;b.title=[ch.apply,ch.reason].filter(Boolean).join('; ');b.setAttribute('aria-label',ch.label+'; '+ch.apply);b.setAttribute('aria-pressed',ch.value===s.value?'true':'false');b.onclick=()=>send('set',{id:s.id,value:ch.value});seg.append(b);});control.append(seg);
 } else if(s.min!==undefined) {
  const isOff=s.hasOff&&s.value===0;
  if(s.hasOff){const off=el('button','btn'+(isOff?' on':''),'Off');off.setAttribute('aria-pressed',String(isOff));off.onclick=()=>send('set',{id:s.id,value:0});const on=el('button','btn'+(!isOff?' on':''),'On');on.setAttribute('aria-pressed',String(!isOff));on.onclick=()=>send('set',{id:s.id,value:isOff?s.min:s.value});control.append(off,on);}
  const valueText=value=>String(value)+(s.unit?(s.unit==='%'?'':' ')+s.unit:'');
  const i=el('input');i.type='range';i.min=s.min;i.max=s.max;i.step=s.step||1;i.value=isOff?s.min:s.value;i.disabled=isOff;i.setAttribute('aria-label',s.title||s.label);i.setAttribute('aria-valuetext',isOff?'Off':valueText(s.value));const out=el('span','help-copy',isOff?'Off':valueText(s.value));i.oninput=()=>{out.textContent=valueText(i.value);i.setAttribute('aria-valuetext',valueText(i.value));};i.onchange=()=>send('set',{id:s.id,value:Number(i.value)});control.append(i,out);
 } else control.append(el('span','help-copy',s.value===-1?'Custom value preserved':String(s.value??'Unknown')));
 // Keep requirements readable by keyboard and assistive technology, including disabled choices.
 const requirements=el('p','detail-copy');requirements.id='needs-'+s.id;
 requirements.textContent=choices.filter(ch=>ch.reason||ch.requirements).map(ch=>ch.label+': '+[ch.requirements,ch.reason].filter(Boolean).join('; ')).join('\n');
 requirements.hidden=!requirements.textContent;c.append(requirements);
 [...control.querySelectorAll('button,input')].forEach((e,index)=>{e.id='control-'+s.id+'-'+index;if(e.type==='checkbox')e.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();e.click();}};e.setAttribute('aria-describedby',requirements.id);});
 c.append(control);
 const badges=el('div','row');
 if(s.developer){if(measured(s.cost))badges.append(pill(s.cost,'cost'));if(measured(s.vram))badges.append(pill(s.vram,'vram'));}
 badges.append(pill(s.apply,/restart/i.test(s.apply)?'restart':''));if(s.maturity==='experimental')badges.append(pill('Experimental','exp'));c.append(badges);
 if(s.applyNote||s.details||s.reason||s.provenance){const d=el('details'),sum=el('summary','','Details and requirements');d.id='details-'+s.id;sum.id='details-summary-'+s.id;restoreDisclosure(d);d.append(sum,el('p','detail-copy',[s.applyNote,s.details,s.reason,s.provenance].filter(Boolean).join('\n')));c.append(d);}
 return c;
}
function renderSettings() {
 const root=$('section-content');rememberDisclosures(root);root.replaceChildren();
 root.append(el('h2','section-title',labels[section]||section));
 if(section==='Support') {
  const c=el('article','card');c.append(el('h3','','Local report'),el('p','','Preview logs, settings and graphics details before saving a report on your PC. Nothing uploads automatically.'));const b=el('button','btn','Report a problem');b.id='support-report';b.onclick=()=>send('report');c.append(b);root.append(c);
  const memory=state.compatibility||{},mem=el('article','card');mem.append(el('h3','','Video memory'),el('p','help-copy',[memory.vram,memory.memory||'Compatibility not checked'].filter(Boolean).join('\n')));root.append(mem);
  const help=el('article','card');help.append(el('h3','','Comparison and overlays'),el('p','help-copy',"Ctrl+Shift+F9 compares BG3RTX with the game's own image. If an overlay causes problems, disable it for one launch, then restore it."));root.append(help);
  const info=el('article','card');info.style.marginTop='16px';info.append(el('h3','','Compatibility'),el('p','help-copy',[state.compatibility?.gpu,...(state.compatibility?.details||[]).filter(x=>x!==state.compatibility?.memory)].filter(Boolean).join('\n')));root.append(info);
  const setup=el('article','card');setup.style.marginTop='16px';setup.append(el('h3','','Game and graphics'),el('p','detail-copy',state.gamePath||'Choose the game folder containing bin\\bg3.exe.'));
  const actions=el('div','row');const browse=el('button','btn','Choose game folder…');browse.id='support-browse';browse.onclick=()=>send('browseGame');const recheck=el('button','btn','Recheck graphics');recheck.id='support-recheck';recheck.onclick=()=>send('recheck');actions.append(browse,recheck);setup.append(actions,el('p','detail-copy','Launches the Vulkan version.'));
  if((state.compatibility?.adapters||[]).length>1){const label=el('label','help-copy','GPU '),select=el('select','btn');select.id='support-adapter';select.setAttribute('aria-label','GPU');const unknown=el('option','','Choose GPU');unknown.value='-1';select.append(unknown);state.compatibility.adapters.forEach(a=>{const option=el('option','',a.label);option.value=a.value;option.selected=a.value===state.compatibility.selected;select.append(option);});select.onchange=()=>send('adapter',{value:Number(select.value)});label.append(select);setup.append(label);}root.append(setup);const notices=el('a','help-copy','Fonts and third-party notices');notices.href='https://app.bg3rtx.local/notices';notices.onclick=e=>{e.preventDefault();send('notices');};root.append(notices,el('p','help-copy',"Independent fan mod; not affiliated with the game's developers."));return;
 }
 const lightingOrder=["rt_geometry", "rr", "sun_shadows", "gi", "exp_hidden_surface", "local_shadows", "box_lights", "exp_native_coverage", "rt_intensity"];
 const all=(state.settings||[]).filter(s=>section==='Developer'?state.developer&&s.developer:s.category===section&&!s.developer);
 if(section==='Lighting')all.sort((a,b)=>{const rank=s=>lightingOrder.includes(s.id)?lightingOrder.indexOf(s.id):lightingOrder.length;return rank(a)-rank(b);});
 const isExperimental=s=>s.experimental===true||(s.experimental===undefined&&s.maturity==='experimental');
 const regular=all.filter(s=>!isExperimental(s)),experimental=all.filter(isExperimental);
 const grid=el('div','grid');regular.forEach(s=>grid.append(card(s)));root.append(grid);
 // One disclosure authorizes experimental choices; policy remains in the bridge.
 if(state.developer&&(experimental.length||section==='Advanced')) {
  const d=el('details','exp');d.open=experimentalOpen;const summary=el('summary');summary.id='experimental-summary';summary.append(pill('Experimental','exp'),el('span','','Unqualified features. Cost and output may vary.'));d.append(summary);const g=el('div','grid');experimental.forEach(s=>g.append(card(s)));if(!experimental.length)g.append(el('p','help-copy','Experimental choices are available on their feature cards when expanded.'));d.append(g);d.ontoggle=()=>{if(experimentalOpen!==d.open){experimentalOpen=d.open;send('experimental',{value:d.open});}};root.append(d);
 }
}

function render(next) {
 const focused=document.activeElement?.id;
 const focusedDialog=['confirmation','report-dialog'].find(id=>$(id).open&&$(id).contains(document.activeElement));
 state=next;if(state.experimental!==undefined)experimentalOpen=state.experimental;
 if(['Advanced','Developer','Memory'].includes(section)&&!state.developer)section='Lighting';
 $('report-progress').hidden=true;$('cancel-report').hidden=true;$('prepare-report').disabled=false;
 const presets=$('presets');presets.replaceChildren();['Low','Medium','High','Ultra'].forEach((name,index)=>{const b=el('button',!state.custom&&state.preset===index?'on':'',name);b.id='preset-'+index;b.setAttribute('aria-pressed',!state.custom&&state.preset===index?'true':'false');b.title=index===state.recommendedPreset?'Suggested for this GPU; performance varies.':'';b.onclick=()=>send('preset',{value:index});presets.append(b);});if(state.custom){const custom=el('span','custom-preset','Custom');custom.setAttribute('aria-label','Custom settings');presets.append(custom);}
 const nav=$('navigation');nav.replaceChildren();const list=sections.filter(([id])=>state.developer||id!=='Advanced');if(state.developer&&(state.settings||[]).some(s=>s.developer))list.splice(list.length-1,0,['Developer','M4 4h16v16H4z']);
 list.forEach(([id,path])=>{const b=el('button',id===section?'on':'');b.id='nav-'+id;b.setAttribute('aria-current',id===section?'page':'false');const glyph=el('img','glyph');glyph.alt='';glyph.src=(localFiles?assetBase+'branding/':'https://branding.bg3rtx.local/')+'ornaments/glyph-'+({Lighting:'light',Surfaces:'water',Performance:'performance',Memory:'memory',Advanced:'performance',Developer:'memory',Support:'help'}[id])+'.svg';b.append(glyph);b.append(document.createTextNode(labels[id]||id));b.onclick=()=>{section=id;render(state);$('main').scrollTop=0;};nav.append(b);});const foot=el('div','foot');if(sample)foot.append(el('div','sample-label','Sample data · no game launch'));nav.append(foot);
 $('hero-version').textContent=state.version||'Version unavailable';
 $('hero-preset').textContent=(state.custom?'Custom':state.presetLabel||['Low','Medium','High','Ultra'][state.preset]||'Custom')+(state.unsavedCount?' (unsaved changes)':'');
 $('hero-gpu').textContent=[state.compatibility?.gpu,measured(state.compatibility?.vram)?state.compatibility.vram:null].filter(Boolean).join(' / ')||'Not detected';
 $('hero-status').textContent=state.modStatus||'Not checked';
 const warnings=$('warnings');warnings.replaceChildren();([]).filter(Boolean).forEach(w=>warnings.append(el('div','notice',w)));if(state.error)warnings.append(el('div','notice error',state.error));
 const count=state.unsavedCount??state.dirty??0;let status=state.status||(count?count+' unsaved '+(count===1?'change':'changes'):'Saved.');const banner=$('restart-banner');banner.hidden=!state.restartNeeded;banner.textContent=state.restartBanner||'';$('status').textContent=status;$('discard').disabled=!count;$('save').disabled=!count;$('play').disabled=!state.canPlay;
 renderSettings();renderPreview();renderReport();
 if(focused){const target=$(focused);if(target&&((focusedDialog&&$(focusedDialog).open&&$(focusedDialog).contains(target))||(!$('confirmation').open&&!$('report-dialog').open)))target.focus({preventScroll:true});}
}
function renderPreview() {
 const p=state.preview,d=$('confirmation');if(!p?.kind){if(d.open)d.close();return;}const root=$('confirmation-copy');root.replaceChildren();$('confirmation-title').textContent=p.kind==='preset'?'Review preset: '+(p.presetLabel||state.presetLabel):'Review required settings';const ul=el('ul','help-copy');(p.changes||[]).forEach(c=>ul.append(el('li','',c)));root.append(ul);(p.blocked||[]).forEach(c=>root.append(el('p','notice',c)));$('confirm-preview').disabled=!!p.blocked?.length;showDialog(d);
}
function renderReport() {
 const r=state.report,d=$('report-dialog');if(!r?.visible){if(d.open)d.close();return;}$('privacy').textContent=r.privacy||'Saves selected logs, settings and graphics details as a ZIP on your PC. Personal paths are partly removed; review every file before sharing. Nothing is uploaded. Saves, screenshots and crash dumps are excluded.';
 const root=$('report-preview'),notice=state.status&&/report|package/i.test(state.status)?state.status:'';
 const signature=JSON.stringify([r,notice]);if(signature===reportSignature){showDialog(d);return;}reportSignature=signature;rememberDisclosures(root);root.replaceChildren();if(r.message)root.append(el('p','help-copy',r.message));if(state.status&&/report|package/i.test(state.status))root.append(el('p','notice',state.status));const files=r.files||[];if(files.length){root.append(el('h3','','Preview · '+files.length+' files · '+(r.bytes||0).toLocaleString()+' bytes'));const ul=el('ul','report-list');files.forEach(f=>{const li=el('li');li.append(el('b','',f.name+' · '+f.size.toLocaleString()+' bytes'));if(f.note)li.append(el('p','',f.note));const details=el('details'),summary=el('summary','','View file contents');details.id='report-file-'+encodeURIComponent(f.name);summary.id='report-summary-'+encodeURIComponent(f.name);restoreDisclosure(details);details.append(summary,el('pre','',f.text||''));li.append(details);ul.append(li);});root.append(ul);}$('export-report').hidden=!files.length;$('prepare-report').textContent=files.length?'Refresh preview':'Preview files';showDialog(d);
}
$('discard').onclick=()=>send('discard');$('save').onclick=()=>send('save',{policy:'enable'});$('play').onclick=()=>send('play');$('cancel-preview').onclick=()=>send('cancel');$('confirm-preview').onclick=()=>send('confirm');$('confirmation').addEventListener('cancel',e=>{e.preventDefault();send('cancel');});
$('close-report').onclick=()=>{$('report-dialog').close();send('closeReport');};$('report-dialog').addEventListener('cancel',()=>send('closeReport'));$('prepare-report').onclick=()=>{if(!sample){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report…';$('cancel-report').hidden=false;$('prepare-report').disabled=true;}send('prepareReport',{issue:$('issue').value,logs:$('include-logs').checked,settings:$('include-settings').checked,system:$('include-system').checked});};$('cancel-report').onclick=()=>send('cancelReport');$('export-report').onclick=()=>send('exportReport');
window.chrome?.webview?.addEventListener('message',e=>{const data=typeof e.data==='string'?JSON.parse(e.data):e.data;if(data.type==='state')render(data);else if(data.type==='progress'){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report: '+data.done+' / '+data.total;$('cancel-report').hidden=false;}});
if(sample) {

 const choices=(labels)=>labels.map((label,value)=>({label,value,enabled:true}));
 const settings=[
 ['rr','DLSS Ray Reconstruction','Lighting',['Off','On'],1,'Reduces noise in ray-traced lighting.'],
 ['rt_geometry','Ray-tracing detail','Lighting',['Off','Solid surfaces','Include foliage'],2,'Includes solid surfaces, or foliage, grates and chains too, in ray-traced lighting and shadows.'],
 ['sun_shadows','Sun shadows','Lighting',['Visible surfaces','Ray-traced','Include hidden roofs'],2,'Adds sun shadows, including shadows from roofs and canopies hidden by the camera.'],
 ['gi','Indirect lighting','Lighting',['Off','Visible surfaces','Ray-traced','Ray-traced high'],3,'Adds bounced light and shading where surfaces meet, with less noise at the highest level.'],
 ['local_shadows','Local light shadows','Lighting',['Off','Fast','Full'],2,'Adds shadows from torches, lamps and spells.'],
 ['rt_intensity','Mod lighting strength','Lighting',[],100,"Blends indirect lighting, local shadows and reflections toward the game's original appearance at lower values."],
 ['exp_hidden_surface','Off-screen bounce light','Lighting',['Off','Near','Far (6 m)'],2,"Adds bounced light from surfaces outside the camera's view."],
 ['box_lights','Interior light shadows','Lighting',['Off','On'],1,'Adds shadows from rectangular interior lights that are meant to cast them. About 0.13 ms on an RTX 5090 at Grymforge.'],
 ['rr_post_temporal','Depth of field and bloom smoothing','Surfaces',['Off','On'],1,'Blends background blur and bloom across frames to smooth fine edges. About 0.15 ms on an RTX 5090 in dialogue.'],
 ['exp_native_coverage','Hair shadows','Lighting',['Off','On'],1,"Adds shadows and contact shading from the game's hair, including on faces and necks."],
 ['reflections','Reflections','Surfaces',['Off','Glossy','Standard','Rough'],3,'Adds reflections to smooth surfaces and, at higher levels, rougher surfaces.'],
 ['exp_water','Water lighting','Surfaces',['Off','Reflections','Reflections and shadows','Include refraction'],3,'Adds water reflections, shadows and a distorted view of the riverbed or pool floor.'],
 ['frame_generation','Frame Generation','Performance',['Off','2x','3x','4x'],1,'Adds generated frames between rendered frames for smoother motion.'],
 ['fps_cap','Rendered frame limit','Performance',[],58,'Limits the frame rate before Frame Generation adds frames.'],
 ['reflex','NVIDIA Reflex','Performance',['Off','On','On + Boost'],1,'Reduces latency; frame generation requires On or Boost.']
 ].map(([id,title,category,levels,value,description])=>({id,title,category,value,description,choices:choices(levels),cost:'Not measured (sample)',apply:['rt_geometry','exp_hidden_surface','exp_native_coverage','exp_water','frame_generation'].includes(id)?'Restart the game to apply':'Applies after saving',...(id==='rt_intensity'?{min:50,max:100,step:5,unit:'%'}:id==='fps_cap'?{min:30,max:240,step:1,unit:'fps',hasOff:true}:{})}));
 render({type:'state',version:'0.9.0.0',modStatus:'Sample preview; game launch disabled',preset:3,presetLabel:'Ultra',recommendedPreset:3,frameGeneration:'2x',frameGenerationNote:'Configured; displayed frames include generated frames',dirty:1,unsavedCount:1,canPlay:false,compatibility:{gpu:'NVIDIA GeForce RTX 5090',vram:'32 GB',driver:'Driver 621.01',warnings:[]},settings,status:'1 unsaved change · Sample preview'});
 if(['error','missing','probe'].includes(sampleView)){if(sampleView==='error')state.error='Settings could not be saved. The destination could not be replaced because another application is using it. Your edits are preserved. Close the application and try again. Windows error 32: the process cannot access the file because it is being used by another process.';if(sampleView==='missing'){state.gamePath='';state.compatibility.warnings=['Choose the game folder containing bin\\bg3.exe. Settings browsing remains available.'];}if(sampleView==='probe'){state.compatibility={gpu:'Graphics unknown',vram:'Unknown',driver:'Unknown',warnings:['Graphics compatibility could not be checked. Recheck graphics or choose an adapter in Help & support.'],adapters:[{value:0,label:'Discrete graphics adapter'},{value:1,label:'Integrated graphics adapter'}],selected:-1};}render(state);}
 if(sampleView==='restart'){state.dirty=0;state.unsavedCount=0;state.status='Saved';state.restartNeeded=true;state.restartCount=3;state.restartBanner='Restart BG3 to apply 3 changes';render(state);}
 if(sampleView==='experimental')$('main').scrollTop=$('main').scrollHeight;
 if(sampleView==='missing')$('main').scrollTop=90;
 if(query.has('report')||sampleView==='report'){state.report={visible:true,privacy:'Creates a ZIP on your PC with BG3RTX logs, graphics settings, version and system graphics details. Logs may contain file paths. We remove common personal path details, but you should review the files before sharing. Nothing is uploaded. Saves, screenshots and crash dumps are excluded.',files:[{name:'CONTENTS.txt',size:248,text:'Sample preview only. No files have been collected.'},{name:'graphics.txt',size:112,text:'Sample adapter: NVIDIA GeForce RTX 5090\nDriver: 621.01\nAdapter used by the game: unconfirmed'}],bytes:360};renderReport();}
} else send('init');
