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
document.querySelector('.emblem').src=localFiles?assetBase+'branding/bg3rtx.svg':'https://branding.bg3rtx.local/bg3rtx.svg';
// The native window paints the real title bar; ?chrome=1 previews it in a plain browser.
if(query.has('chrome'))$('titlebar').hidden=false;
let state = null;
const sampleView=query.get('view')||'';
// Review views (--screenshot-view) open the page they show.
const viewSection={missing:'Support',probe:'Support',support:'Support',surfaces:'Surfaces',performance:'Performance',experimental:'Experimental',advanced:'Advanced'};
let section = query.get('section') || viewSection[sampleView] || 'Lighting';
let experimentalOpen = query.has('experimental')||sampleView==='experimental';
const disclosureState=new Map();
let reportSignature=null;
function rememberDisclosures(root) {root.querySelectorAll('details').forEach(d=>{if(d.id)disclosureState.set(d.id,d.open);});}
function restoreDisclosure(d) {d.open=disclosureState.get(d.id)||false;}
// One stroke style: 24-unit grid, 1.75 stroke, round caps (see .icon svg in app.css).
const icons = {
 Lighting:'<circle cx="12" cy="12" r="4"/><path d="M12 2.5V5M12 19v2.5M2.5 12H5M19 12h2.5M5.3 5.3l1.8 1.8M16.9 16.9l1.8 1.8M5.3 18.7l1.8-1.8M16.9 7.1l1.8-1.8"/>',
 Surfaces:'<path d="M12 3.2c3.4 4 5.8 7.2 5.8 10.3a5.8 5.8 0 0 1-11.6 0c0-3.1 2.4-6.3 5.8-10.3z"/><path d="M9.1 14.1a3 3 0 0 0 2.6 2.6"/>',
 Performance:'<path d="M4.4 17.6a8.6 8.6 0 1 1 15.2 0"/><path d="M12 14.6l4.2-4.9"/><circle cx="12" cy="14.6" r="1.4"/><path d="M6.6 11.6l1.3.6M12 6.4v1.5M17.4 11.6l-1.3.6"/>',
 Experimental:'<path d="M9.5 3.5h5M10.6 3.5v5.9L5.3 18.1a1.6 1.6 0 0 0 1.4 2.4h10.6a1.6 1.6 0 0 0 1.4-2.4l-5.3-8.7V3.5"/><path d="M7.5 14.6h9"/>',
 Support:'<circle cx="12" cy="12" r="9"/><path d="M9.6 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.3-1.1.8-1.1 1.5v.6"/><path d="M12 16.9v.2"/>',
 Advanced:'<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
 Developer:'<path d="M8.5 7.5 4 12l4.5 4.5M15.5 7.5 20 12l-4.5 4.5M13.5 5.5l-3 13"/>',
 Needs:'<circle cx="12" cy="12" r="9"/><path d="M12 7.5v5.5M12 16.4v.2"/>'
};
function icon(name) {const s=el('span','icon');s.setAttribute('aria-hidden','true');s.innerHTML='<svg viewBox="0 0 24 24" focusable="false">'+icons[name]+'</svg>';return s;}
icons['DLSS 5']=icons.Performance;
const sections = ['Lighting','Surfaces','DLSS 5','Performance','Experimental','Advanced','Support'];
const labels = {Support:'Help & support'};
const leads = {
 Lighting:'Ray-traced shadows, bounced light and the strength of the mod.',
 Surfaces:'Reflections, water and post-effect smoothing.',
 Performance:'Frame rate, latency and frame generation.',
 'DLSS 5':'Experimental neural rendering. Requires Ray Reconstruction and your own runtime file.',
 Experimental:'Not fully tested. May change the image or cost performance.',
 Support:'Game folder, graphics checks and problem reports.'
};
function el(tag, className, text) { const e=document.createElement(tag);if(className)e.className=className;if(text!==undefined)e.textContent=text;return e; }
function send(type, extra={}) { if(sample) { $('status').textContent='Sample preview — actions do not change settings or launch the game';return; } window.chrome?.webview?.postMessage({type,...extra}); }
function measured(text) {return !!text && !/unknown|not measured|not detected|unavailable/i.test(text);}
function pill(text,kind) {return el('span','pill '+(kind||''),text);}
function showDialog(d) {if(!d.open)d.showModal();}
const releaseOptIn=()=>!!state&&!state.developer&&!!state.experimental;
const optInPage=s=>!state.developer&&s.category==='Experimental';
// Release copy on the Experimental page drops the metadata's own "Experimental:" and "(unqualified)" markers.
const plainLabel=(s,text)=>optInPage(s)?text.replace(/\s*\((?:unqualified|experimental|advanced)\)/gi,''):text;
// Short apply badge; the full sentence stays in the title and in Details.
// A card whose current choice applies live can still have choices that need a restart (water On, Frame Generation Off).
function applyBadge(apply,choices) {
 if(/restart/i.test(apply))return el('span','badge restart','Restart to apply');
 if((choices||[]).some(ch=>/restart/i.test(ch.apply||'')))return el('span','badge restart','May need a restart');
 if(/pause/i.test(apply))return el('span','badge pause','May briefly pause');
 return el('span','badge','Applies after saving');
}
// Measured costs written at the end of a description move to their own muted line.
function splitCost(text) {const m=/^(.*?)\s*((?:About|Around) \d[\d.]*(?:[–-]\d[\d.]*)? ms\b.*)$/.exec(text||'');return m?[m[1],m[2]]:[text||'',''];}
// Unmet requirements only, once per card: "Needs X" or "Rough needs X".
function needsText(s) {
 const choices=(s.choices||[]),byReason=new Map();
 choices.forEach(ch=>{if(ch.reason){if(!byReason.has(ch.reason))byReason.set(ch.reason,[]);byReason.get(ch.reason).push(plainLabel(s,ch.label));}});
 const upgrades=choices.slice(1).length;
 return [...byReason].map(([reason,names])=>{
  if(!/^Needs /.test(reason)||names.length>=upgrades||choices.length<=2)return reason;
  return names.join(', ').replace(/, ([^,]*)$/,' and $1')+(names.length>1?' need ':' needs ')+reason.slice(6);
 }).join('\n');
}
// Full requirements for Details, grouped by identical lists.
function requirementLines(s) {
 const groups=new Map();
 (s.choices||[]).forEach(ch=>{if(ch.requirements){if(!groups.has(ch.requirements))groups.set(ch.requirements,[]);groups.get(ch.requirements).push(plainLabel(s,ch.label));}});
 return [...groups].map(([req,names])=>names.join(', ')+': '+req);
}
function choiceControl(s,control,describedBy) {
 const choices=s.choices,name=s.title||s.label;
 const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-label',name);
 choices.forEach(ch=>{const text=plainLabel(s,ch.label),b=el('button',ch.value===s.value?'on':'',text);b.disabled=ch.enabled===false;b.title=[ch.apply,ch.reason].filter(Boolean).join('. ');b.setAttribute('aria-label',text+'; '+ch.apply);b.setAttribute('aria-pressed',ch.value===s.value?'true':'false');b.onclick=()=>send('set',{id:s.id,value:ch.value});seg.append(b);});
 // Shown instead of the buttons when they do not fit the card (fitChoices).
 const wrap=el('span','select-wrap'),select=el('select','choice-select');select.id='control-'+s.id+'-select';select.setAttribute('aria-label',name);select.setAttribute('aria-describedby',describedBy);
 if(s.value<0){const o=el('option','','Custom value');o.value='-1';o.selected=true;o.disabled=true;select.append(o);}
 choices.forEach(ch=>{const o=el('option','',plainLabel(s,ch.label));o.value=String(ch.value);o.selected=ch.value===s.value;o.disabled=ch.enabled===false;select.append(o);});
 select.onchange=()=>send('set',{id:s.id,value:Number(select.value)});wrap.append(select);
 control.className+=' choices';control.append(seg,wrap);
}
function card(s) {
 const c=el('article','card');c.id='card-'+s.id;
 const head=el('div','card-head');head.append(el('h3','',s.title||s.label));
 const badge=applyBadge(s.apply||'',s.choices);badge.title=[s.apply,...(s.choices||[]).filter(ch=>/restart/i.test(ch.apply||'')).map(ch=>plainLabel(s,ch.label)+': '+ch.apply)].filter(Boolean).join('\n');head.append(badge);c.append(head);
 let [description,cost]=splitCost(s.description||s.summary||'');
 if(optInPage(s))description=description.replace(/^Experimental:\s*/,'').replace(/^./,x=>x.toUpperCase());
 c.append(el('p','description',description+(s.developer?' GPU cost: '+(s.cost||'Not measured.') : '')));
 const needs=el('p','needs');needs.id='needs-'+s.id;needs.append(icon('Needs'),el('span','',needsText(s)));needs.hidden=!needsText(s);c.append(needs);
 const foot=el('div','card-foot'),control=el('div','control');
 const choices=s.choices||[];
 if(s.custom)foot.append(el('p','help-copy','Custom value kept. Choose an option to replace it.'));
 if(choices.length===2&&choices[0].label==='Off'&&plainLabel(s,choices[1].label)==='On') {
  const l=el('label','toggle'),i=el('input');i.type='checkbox';i.checked=s.value===choices[1].value;i.disabled=choices[i.checked?0:1].enabled===false;i.setAttribute('aria-label',s.title||s.label);i.title=[choices[i.checked?0:1].apply,choices[i.checked?0:1].reason].filter(Boolean).join('. ');i.onchange=()=>send('set',{id:s.id,value:choices[i.checked?1:0].value});l.append(i,el('span'));control.append(l,el('span','toggle-state',i.checked?'On':'Off'));
 } else if(choices.length) {
  choiceControl(s,control,needs.id);
 } else if(s.min!==undefined) {
  const isOff=s.hasOff&&s.value===0;
  if(s.hasOff){const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-label',s.title||s.label);const off=el('button',isOff?'on':'','Off');off.setAttribute('aria-pressed',String(isOff));off.onclick=()=>send('set',{id:s.id,value:0});const on=el('button',!isOff?'on':'','On');on.setAttribute('aria-pressed',String(!isOff));on.onclick=()=>send('set',{id:s.id,value:isOff?s.min:s.value});seg.append(off,on);control.append(seg);}
  const valueText=value=>String(value)+(s.unit?(s.unit==='%'?'':' ')+s.unit:'');
  const i=el('input');i.type='range';i.min=s.min;i.max=s.max;i.step=s.step||1;i.value=isOff?s.min:s.value;i.disabled=isOff;i.setAttribute('aria-label',s.title||s.label);i.setAttribute('aria-valuetext',isOff?'Off':valueText(s.value));const out=el('output','range-value',isOff?'Off':valueText(s.value));i.oninput=()=>{out.textContent=valueText(i.value);i.setAttribute('aria-valuetext',valueText(i.value));};i.onchange=()=>send('set',{id:s.id,value:Number(i.value)});control.className+=' range';control.append(i,out);
 } else control.append(el('span','help-copy',s.value===-1?'Custom value preserved':String(s.value??'Unknown')));
 [...control.querySelectorAll('button,input')].forEach((e,index)=>{e.id='control-'+s.id+'-'+index;if(e.type==='checkbox')e.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();e.click();}};e.setAttribute('aria-describedby',needs.id);});
 foot.append(control);
 if(cost)foot.append(el('p','cost',cost));
 if(s.developer){const badges=el('div','row');if(measured(s.cost))badges.append(pill(s.cost,'cost'));if(measured(s.vram))badges.append(pill(s.vram,'vram'));if(s.maturity==='experimental')badges.append(pill('Experimental','exp'));if(badges.children.length)foot.append(badges);}
 const requirements=requirementLines(s);
 const detail=[s.apply,s.applyNote,s.details,s.reason,s.provenance].filter(Boolean);
 const d=el('details'),sum=el('summary','','Details and requirements');d.id='details-'+s.id;sum.id='details-summary-'+s.id;restoreDisclosure(d);d.append(sum);
 if(detail.length)d.append(el('p','detail-copy',detail.join('\n')));
 if(requirements.length){d.append(el('p','detail-heading','Requirements'));d.append(el('p','detail-copy',requirements.join('\n')));}
 if(detail.length||requirements.length)foot.append(d);
 if(s.id==='dlss5_nr') {
  const runtime=el('div','runtime-file'),path=el('p','detail-copy path',state.dlss5RuntimePath||'No file selected - automatic discovery');
  path.id='dlss5-runtime-path';runtime.append(el('p','detail-heading','DLSS 5 runtime file'),path,el('p','help-copy',state.dlss5RuntimeStatus||'Your runtime is prepared before Play; it is not included with BG3RTX.'));
  const row=el('div','row'),browse=el('button','btn','Browse...');browse.id='dlss5-runtime-browse';browse.setAttribute('aria-describedby','needs-'+s.id);browse.onclick=()=>send('browseDlss5Runtime');row.append(browse);
  if(state.dlss5RuntimePath){const clear=el('button','btn','Use automatic discovery');clear.id='dlss5-runtime-clear';clear.setAttribute('aria-describedby','needs-'+s.id);clear.onclick=()=>send('clearDlss5Runtime');row.append(clear);}
  runtime.append(row);foot.append(runtime);
 }
 c.append(foot);
 return c;
}
// Segmented choices size to their labels; a card too narrow for them gets the dropdown instead.
function fitChoices() {
 if(!document.querySelectorAll)return;
 document.querySelectorAll('.control.choices').forEach(c=>{
  const seg=c.querySelector('.seg'),focused=document.activeElement;
  c.classList.remove('compact');
  if(seg.offsetWidth>c.clientWidth+1){c.classList.add('compact');if(seg.contains(focused))c.querySelector('select').focus({preventScroll:true});}
 });
}
if(window.addEventListener)window.addEventListener('resize',fitChoices);
document.fonts?.ready?.then(fitChoices);
function sectionHead(root,id) {const head=el('div','section-head');head.append(el('h2','section-title',labels[id]||id));if(leads[id])head.append(el('p','section-lead',leads[id]));root.append(head);}
function shortcutsCard(id) {
 const box=el('article','card shortcuts');box.append(el('h3','','Keyboard shortcuts'));
 const name=id==='hotkey_rt_toggle'?'All ray tracing off/on':'DLSS 5 off/on';
 const label=el('label','shortcut-label',name),input=el('input');input.type='text';input.readOnly=true;input.id=id;
 input.value=state.shortcuts?.[id]||(id==='hotkey_rt_toggle'?'Ctrl+Shift+F9':'Ctrl+Shift+F8');
 input.placeholder='Press a key combination';input.setAttribute('aria-label',name+' ? press a key combination');
 const help=el('p','help-copy','Select the field and press a key combination. Delete or Backspace disables it. Escape cancels.');
 input.onfocus=()=>{input.value='';};input.onblur=()=>{input.value=state.shortcuts?.[id]||'None';};
 input.onkeydown=e=>{
  if(e.key==='Tab')return;
  e.preventDefault();e.stopPropagation();
  if(e.repeat)return;
  if(e.key==='Escape'){input.blur();return;}
  let value;
  if(e.key==='Delete'||e.key==='Backspace')value='None';
  else {
   let key=e.key.toUpperCase();
   // Shift changes digit labels (for example 1 becomes !), but the host uses VK_1.
   if(/^Digit[0-9]$/.test(e.code||''))key=e.code.slice(-1);
   if(!/^(F[1-9]|F1[0-2]|[A-Z0-9])$/.test(key))return;
   value=(e.ctrlKey?'Ctrl+':'')+(e.shiftKey?'Shift+':'')+(e.altKey?'Alt+':'')+key;
  }
  const other=id==='hotkey_dlss5'?'hotkey_rt_toggle':'hotkey_dlss5';
  if(value!=='None'&&((e.ctrlKey&&e.shiftKey&&e.key.toUpperCase()==='F10')||value===state.shortcuts?.[other])){help.textContent='This shortcut is already in use. Choose another combination.';input.value=value;return;}
  send('shortcut',{id,value});
 };
 const reset=el('button','btn','Reset to default');reset.onclick=()=>send('resetShortcut',{id});
 const disable=el('button','btn','None');disable.onclick=()=>send('shortcut',{id,value:'None'});
 input.setAttribute('aria-describedby',id+'-help');help.id=id+'-help';
 const actions=el('div','row');actions.append(reset,disable);label.append(input);box.append(label,help,actions);
 if(id==='hotkey_dlss5')box.append(el('p','help-copy','Works while playing when DLSS 5 was On at game start.'));
 return box;
}
// First-run preparation (docs/RTX_HAIR_ROADMAP.md 2b): read-only status lines the host sends as state.preparation,
// [{id,title,line,detail,progress (0..1 or absent),state:'done'|'running'|'waiting'|'paused'}]. No actions yet.
function preparationCard(items) {
 const box=el('article','card preparation');box.id='preparation';box.append(el('h3','','Preparation'));
 items.forEach(p=>{
  const row=el('div','prep-item'),head=el('div','prep-head');row.id='prep-'+p.id;
  head.append(el('span','prep-title',p.title),el('span','badge'+(p.state==='paused'?' restart':''),{done:'Ready',running:'Working',waiting:'Waiting',paused:'Paused'}[p.state]||p.state||''));
  row.append(head,el('p','description',p.line||''));
  if(typeof p.progress==='number'&&p.state!=='done'){const bar=el('progress');bar.max=1;bar.value=p.progress;bar.setAttribute('aria-label',p.title+' progress');row.append(bar);}
  if(p.detail)row.append(el('p','cost',p.detail));
  box.append(row);
 });
 return box;
}
function renderSettings() {
 const root=$('section-content');rememberDisclosures(root);root.replaceChildren();
 sectionHead(root,section);
 if(section==='Support') {
  const grid=el('div','grid support');
  if((state.preparation||[]).length)grid.append(preparationCard(state.preparation));
  const report=el('article','card');report.append(el('h3','','Report a problem'),el('p','description','Preview logs, settings and graphics details before saving a report on your PC. Nothing uploads automatically.'));const b=el('button','btn','Report a problem…');b.id='support-report';b.onclick=()=>send('report');const rf=el('div','card-foot');rf.append(b);report.append(rf);grid.append(report);
  (state.settings||[]).filter(s=>s.category==='Support'&&!s.developer).forEach(s=>grid.append(card(s)));
  const setup=el('article','card');setup.append(el('h3','','Game and graphics'),el('p','description path',state.gamePath||'Choose the game folder containing bin\\bg3.exe.'));
  const actions=el('div','row');const browse=el('button','btn','Choose game folder…');browse.id='support-browse';browse.onclick=()=>send('browseGame');const recheck=el('button','btn','Recheck graphics');recheck.id='support-recheck';recheck.onclick=()=>send('recheck');actions.append(browse,recheck);
  const sf=el('div','card-foot');sf.append(actions);
  if((state.compatibility?.adapters||[]).length>1){const label=el('label','adapter','GPU '),wrap=el('span','select-wrap'),select=el('select');select.id='support-adapter';select.setAttribute('aria-label','GPU');const unknown=el('option','','Choose GPU');unknown.value='-1';select.append(unknown);state.compatibility.adapters.forEach(a=>{const option=el('option','',a.label);option.value=a.value;option.selected=a.value===state.compatibility.selected;select.append(option);});select.onchange=()=>send('adapter',{value:Number(select.value)});wrap.append(select);label.append(wrap);sf.append(label);}
  sf.append(el('p','cost','Play starts the Vulkan version of the game (bin\\bg3.exe) with BG3RTX.'));setup.append(sf);grid.append(setup);
  const memory=state.compatibility||{},mem=el('article','card');mem.append(el('h3','','Video memory'),el('p','description pre',memory.memory||(measured(memory.vram)?memory.vram:'Not checked yet. Use Recheck graphics.')));grid.append(mem);
  const info=el('article','card');info.append(el('h3','','Compatibility'),el('p','description pre',[state.compatibility?.gpu,...(state.compatibility?.details||[]).filter(x=>x&&x!==state.compatibility?.memory)].filter(Boolean).join('\n')));grid.append(info);
  const help=el('article','card');help.append(el('h3','','Comparison and overlays'),el('p','description pre',"By default, Ctrl+Shift+F9 in game switches all ray-traced lighting off and on, to compare with the game's own image. Change this shortcut on Advanced. DLSS and Frame Generation stay as they are.\nIf the game does not start or Frame Generation does nothing, turn off other overlays (Steam, Discord, GOG Galaxy) for one launch."));grid.append(help);
  root.append(grid);
  const foot=el('div','page-foot');const notices=el('a','link','Fonts and third-party notices');notices.href='https://app.bg3rtx.local/notices';notices.onclick=e=>{e.preventDefault();send('notices');};foot.append(notices,el('p','help-copy',"Independent fan mod; not affiliated with the game's developers."));root.append(foot);return;
 }
 const lightingOrder=["rt_geometry", "rr", "sun_shadows", "gi", "exp_hidden_surface", "local_shadows", "box_lights", "exp_native_coverage", "rt_intensity"];
 const all=(state.settings||[]).filter(s=>section==='Developer'?state.developer&&s.developer:s.category===section&&!s.developer);
 const performanceOrder=['frame_generation','reflex','fps_cap'];
 if(section==='Performance')all.sort((a,b)=>{const rank=s=>performanceOrder.includes(s.id)?performanceOrder.indexOf(s.id):performanceOrder.length;return rank(a)-rank(b);});
 if(section==='Lighting')all.sort((a,b)=>{const rank=s=>lightingOrder.includes(s.id)?lightingOrder.indexOf(s.id):lightingOrder.length;return rank(a)-rank(b);});
 if(section==='Experimental'&&!state.developer) {
  // Release opt-in page: grouped by what the options touch.
  const group=s=>/water/.test(s.id)?'Water':/hair/.test(s.id)?'Strand hair':'Scene capture';
  ['Water','Strand hair','Scene capture'].forEach(name=>{const items=all.filter(s=>group(s)===name);if(!items.length)return;root.append(el('h3','group-title',name));const grid=el('div','grid');items.forEach(s=>grid.append(card(s)));root.append(grid);});
  fitChoices();return;
 }
 const isExperimental=s=>s.id!=='dlss5_nr'&&(s.experimental===true||(s.experimental===undefined&&s.maturity==='experimental'));
 const regular=all.filter(s=>!isExperimental(s)),experimental=all.filter(isExperimental);
 const grid=el('div','grid');regular.forEach(s=>grid.append(card(s)));root.append(grid);
 // Developer mode: one disclosure authorizes experimental choices; policy remains in the bridge.
 if(state.developer&&(experimental.length||section==='Advanced')) {
  const d=el('details','exp');d.open=experimentalOpen;const summary=el('summary');summary.id='experimental-summary';summary.append(pill('Experimental','exp'),el('span','','Unqualified features. Cost and output may vary.'));d.append(summary);const g=el('div','grid');experimental.forEach(s=>g.append(card(s)));if(!experimental.length)g.append(el('p','help-copy','Experimental choices are available on their feature cards when expanded.'));d.append(g);d.ontoggle=()=>{if(experimentalOpen!==d.open){experimentalOpen=d.open;send('experimental',{value:d.open});}};root.append(d);
 }
 if(section==='Advanced'||section==='DLSS 5')root.append(shortcutsCard(section==='Advanced'?'hotkey_rt_toggle':'hotkey_dlss5'));
 fitChoices();
}
function renderNavigation() {
 const nav=$('navigation');nav.replaceChildren();
 const list=sections.filter(id=>id==='Experimental'?releaseOptIn():true);
 if(state.developer&&(state.settings||[]).some(s=>s.developer))list.splice(list.length-1,0,'Developer');
 list.forEach(id=>{const b=el('button',id===section?'on':'');b.id='nav-'+id;b.setAttribute('aria-current',id===section?'page':'false');b.append(icon(id),el('span','',labels[id]||id));b.onclick=()=>{section=id;render(state);$('main').scrollTop=0;};nav.append(b);});
 const foot=el('div','nav-foot');
 if(!state.developer){
  // Release opt-in for unqualified options. Off hides the page and restores the qualified values.
  const row=el('label','switch-row'),text=el('span','switch-text'),i=el('input');const title=el('span','switch-title');title.append(el('span','long','Show experimental options'),el('span','short','Experimental'));text.append(title,el('span','switch-note','Not fully tested'));i.setAttribute('aria-label','Show experimental options');
  const t=el('span','toggle');i.type='checkbox';i.id='experimental-switch';i.checked=releaseOptIn();i.onchange=()=>{if(!i.checked&&section==='Experimental')section='Lighting';send('experimental',{value:i.checked});};i.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();i.click();}};t.append(i,el('span'));row.append(text,t);foot.append(row);
 }
 if(sample)foot.append(el('div','sample-label','Sample data · no game launch'));
 nav.append(foot);
}
function render(next) {
 const focused=document.activeElement?.id;
 const focusedDialog=['confirmation','report-dialog'].find(id=>$(id).open&&$(id).contains(document.activeElement));
 state=next;if(state.experimental!==undefined)experimentalOpen=state.experimental;
 if(['Developer','Memory'].includes(section)&&!state.developer)section='Lighting';
 if(section==='Experimental'&&!releaseOptIn())section='Lighting';
 $('report-progress').hidden=true;$('cancel-report').hidden=true;$('prepare-report').disabled=false;
 const presets=$('presets');presets.replaceChildren();['Low','Medium','High','Ultra'].forEach((name,index)=>{const on=!state.custom&&state.preset===index,b=el('button',on?'on':'',name);b.id='preset-'+index;b.setAttribute('aria-pressed',on?'true':'false');b.title=[state.presetDescriptions?.[index],index===state.recommendedPreset?'Suggested for this GPU; performance varies.':''].filter(Boolean).join('\n');b.onclick=()=>send('preset',{value:index});presets.append(b);});if(state.custom){const custom=el('span','custom-preset','Custom');custom.setAttribute('aria-label','Custom settings');presets.append(custom);}
 renderNavigation();
 $('hero-version').textContent=state.version||'Version unavailable';
 $('hero-gpu').textContent=[state.compatibility?.gpu,measured(state.compatibility?.vram)?state.compatibility.vram:null].filter(Boolean).join(' · ')||'Not detected';
 const mod=$('hero-status');mod.textContent=state.modStatus||'Not checked';mod.className=/missing/i.test(state.modStatus||'')?'warn':/present/i.test(state.modStatus||'')?'ok':'';
 const warnings=$('warnings');warnings.replaceChildren();
 if(state.gamePath===''){const n=el('div','notice setup'),b=el('button','btn','Choose game folder…');b.id='notice-browse';b.onclick=()=>send('browseGame');n.append(el('span','','Baldur\'s Gate 3 was not found. Choose the game folder that contains bin\\bg3.exe to enable Play.'),b);warnings.append(n);}
 if(state.error)warnings.append(el('div','notice error',state.error));
 const count=state.unsavedCount??state.dirty??0;let status=state.status||(count?count+' unsaved '+(count===1?'change':'changes'):'Saved.');const banner=$('restart-banner');banner.hidden=!state.restartNeeded;banner.textContent=state.restartBanner||'';$('status').textContent=status;$('discard').disabled=!count;$('save').disabled=!count;$('play').disabled=!state.canPlay;
 renderSettings();renderPreview();renderReport();
 if(focused){const target=$(focused);if(target&&((focusedDialog&&$(focusedDialog).open&&$(focusedDialog).contains(target))||(!$('confirmation').open&&!$('report-dialog').open)))target.focus({preventScroll:true});}
}
function renderPreview() {
 const p=state.preview,d=$('confirmation');if(!p?.kind){if(d.open)d.close();return;}const root=$('confirmation-copy');root.replaceChildren();$('confirmation-title').textContent=p.kind==='preset'?'Review preset: '+(p.presetLabel||state.presetLabel):'Review required settings';const pi=['Low','Medium','High','Ultra'].indexOf(p.presetLabel);if(p.kind==='preset'&&state.presetDescriptions?.[pi])root.append(el('p','',state.presetDescriptions[pi]));if(!(p.changes||[]).length)root.append(el('p','','No settings change.'));const ul=el('ul','help-copy');(p.changes||[]).forEach(c=>ul.append(el('li','',c)));root.append(ul);(p.blocked||[]).forEach(c=>root.append(el('p','notice',c)));$('confirm-preview').disabled=!!p.blocked?.length;showDialog(d);
}
function renderReport() {
 const r=state.report,d=$('report-dialog');if(!r?.visible){if(d.open)d.close();return;}$('logging-hint').textContent=r.loggingHint||'';$('logging-hint').hidden=!r.loggingHint;$('privacy').textContent=r.privacy||'Saves selected logs, settings and graphics details as a ZIP on your PC. Personal paths are partly removed; review every file before sharing. Nothing is uploaded. Saves, screenshots and crash dumps are excluded.';
 const root=$('report-preview'),notice=state.status&&/report|package/i.test(state.status)?state.status:'';
 const signature=JSON.stringify([r,notice]);if(signature===reportSignature){showDialog(d);return;}reportSignature=signature;rememberDisclosures(root);root.replaceChildren();if(r.message)root.append(el('p','help-copy',r.message));if(state.status&&/report|package/i.test(state.status))root.append(el('p','notice',state.status));const files=r.files||[];if(files.length){root.append(el('h3','','Preview · '+files.length+' files · '+(r.bytes||0).toLocaleString()+' bytes'));const ul=el('ul','report-list');files.forEach(f=>{const li=el('li');li.append(el('b','',f.name+' · '+f.size.toLocaleString()+' bytes'));if(f.note)li.append(el('p','',f.note));const details=el('details'),summary=el('summary','','View file contents');details.id='report-file-'+encodeURIComponent(f.name);summary.id='report-summary-'+encodeURIComponent(f.name);restoreDisclosure(details);details.append(summary,el('pre','',f.text||''));li.append(details);ul.append(li);});root.append(ul);}$('export-report').hidden=!files.length;$('prepare-report').textContent=files.length?'Refresh preview':'Preview files';showDialog(d);
}
$('discard').onclick=()=>send('discard');$('save').onclick=()=>send('save',{policy:'enable'});$('play').onclick=()=>send('play');$('cancel-preview').onclick=()=>send('cancel');$('confirm-preview').onclick=()=>send('confirm');$('confirmation').addEventListener('cancel',e=>{e.preventDefault();send('cancel');});
$('close-report').onclick=()=>{$('report-dialog').close();send('closeReport');};$('report-dialog').addEventListener('cancel',()=>send('closeReport'));$('prepare-report').onclick=()=>{if(!sample){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report…';$('cancel-report').hidden=false;$('prepare-report').disabled=true;}send('prepareReport',{issue:$('issue').value,logs:$('include-logs').checked,settings:$('include-settings').checked,system:$('include-system').checked});};$('cancel-report').onclick=()=>send('cancelReport');$('export-report').onclick=()=>send('exportReport');
window.chrome?.webview?.addEventListener('message',e=>{const data=typeof e.data==='string'?JSON.parse(e.data):e.data;if(data.type==='state'){render(data);if(sample)reviewView(true);}else if(data.type==='progress'){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report: '+data.done+' / '+data.total;$('cancel-report').hidden=false;}});
// Review views (--screenshot-view) adjust the sample, or the host's review state, before rendering again.
function reviewView(host) {
 {if(sampleView==='error')state.error='Settings could not be saved. The destination could not be replaced because another application is using it. Your edits are preserved. Close the application and try again. Windows error 32: the process cannot access the file because it is being used by another process.';if(sampleView==='missing'&&!host){state.gamePath='';state.compatibility.warnings=['Choose the game folder containing bin\\bg3.exe. Settings browsing remains available.'];}if(sampleView==='probe'&&!host){state.compatibility={gpu:'Graphics unknown',vram:'Unknown',driver:'Unknown',warnings:['Graphics compatibility could not be checked. Recheck graphics or choose an adapter in Help & support.'],adapters:[{value:0,label:'Discrete graphics adapter'},{value:1,label:'Integrated graphics adapter'}],selected:-1};}render(state);}
 if(sampleView==='restart'){state.dirty=0;state.unsavedCount=0;state.status='Saved';state.restartNeeded=true;state.restartCount=3;state.restartBanner='Restart the game to apply 3 changes';render(state);}
 if((query.has('report')||sampleView==='report')&&!state.report?.files?.length){state.report={visible:true,privacy:'Creates a ZIP on your PC with BG3RTX logs, graphics settings, version and system graphics details. Logs may contain file paths. We remove common personal path details, but you should review the files before sharing. Nothing is uploaded. Saves, screenshots and crash dumps are excluded.',files:[{name:'CONTENTS.txt',size:248,text:'Sample preview only. No files have been collected.'},{name:'graphics.txt',size:112,text:'Sample adapter: NVIDIA GeForce RTX 5090\nDriver: 621.01\nAdapter used by the game: unconfirmed'}],bytes:360};renderReport();}
 if(sampleView==='experimental'||query.has('bottom'))$('main').scrollTop=$('main').scrollHeight;
}
if(sample) {
 const choices=(labels)=>labels.map((label,value)=>({label,value,enabled:true,apply:'Applies after saving'}));
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
 ['reflex','NVIDIA Reflex','Performance',['Off','On','On + Boost'],1,'Reduces latency; frame generation requires On or Boost.'],
 ['detailed_logging','Detailed logging','Support',['Off','On'],0,'Writes extra performance and diagnostic details to the log for bug reports. Leave it off for normal play.']
 ].map(([id,title,category,levels,value,description])=>({id,title,category,value,description,choices:choices(levels),cost:'Not measured (sample)',apply:['rt_geometry','exp_hidden_surface','exp_native_coverage','exp_water','frame_generation','detailed_logging'].includes(id)?'Restart the game to apply':'Applies after saving',...(id==='rt_intensity'?{min:50,max:100,step:5,unit:'%'}:id==='fps_cap'?{min:30,max:240,step:1,unit:'fps',hasOff:true}:{})}));
 // The WebView host sends its review state (real settings table); a plain browser uses this sample.
 if(!window.chrome?.webview){render({type:'state',version:'0.9.4.0',modStatus:'Sample preview; game launch disabled',preset:3,presetLabel:'Ultra',recommendedPreset:3,frameGeneration:'2x',frameGenerationNote:'Configured; displayed frames include generated frames',dirty:1,unsavedCount:1,canPlay:false,compatibility:{gpu:'NVIDIA GeForce RTX 5090',vram:'32 GB',driver:'Driver 621.01',warnings:[]},settings,status:'1 unsaved change · Sample preview',preparation:[
  {id:'hair',title:'Strand hair',line:'412 of 559 styles ready, 2.1 GB on disk.',detail:'Companions and character creation done. Converting named NPCs, about 35 min left.',progress:0.74,state:'running'},
  {id:'shaders',title:'Shaders',line:'Ultra shaders ready.',detail:'Graphics driver finishes its own cache in the first minute of play.',state:'done'}]});
 reviewView(false);}
} else send('init');
