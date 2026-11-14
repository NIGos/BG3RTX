/* UI is a renderer. The native bridge owns policy, dependencies, edits and persistence.
   Instant apply: a click sends its change and the bridge writes it at once. There is no Save and no review step;
   the bridge answers with what changed (state.notice, with Undo), what waits for a game restart
   (state.restartNames) and, when the file could not be written, state.writeError with Retry.
   Layout (docs/ui_rework/PROPOSAL.md, Direction A): sidebar with Play, preset in the header, setting rows in groups,
   detail panel for the selected row. */
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
(document.querySelectorAll?[...document.querySelectorAll('.emblem')]:[document.querySelector('.emblem')]).forEach(e=>{e.src=localFiles?assetBase+'branding/bg3rtx.svg':'https://branding.bg3rtx.local/bg3rtx.svg';});
// The native window paints the real title bar; ?chrome=1 previews it in a plain browser.
if(query.has('chrome'))$('titlebar').hidden=false;
let state = null;
const sampleView=query.get('view')||'';
// Review views (--screenshot-view) open the page they show.
const viewSection={missing:'Home',notice:'Lighting',probe:'Support',support:'Support',surfaces:'Surfaces',performance:'Performance',experimental:'Experimental',advanced:'Advanced',hotkeys:'Hotkeys',lighting:'Lighting',dlss5:'DLSS 5'};
let section = query.get('section') || viewSection[sampleView] || 'Home';
let experimentalOpen = query.has('experimental')||sampleView==='experimental';
// The selected row of each page (its details show in the panel) and open disclosures survive bridge refreshes.
const selected={};
if(query.get('select'))selected[section]=query.get('select');
const disclosureState=new Map();
let reportSignature=null;
function rememberDisclosures(root) {root.querySelectorAll('details').forEach(d=>{if(d.id)disclosureState.set(d.id,d.open);});}
function restoreDisclosure(d,initial) {d.open=disclosureState.has(d.id)?disclosureState.get(d.id):!!initial;}
// One stroke style: 24-unit grid, 1.75 stroke, round caps (see .icon svg in app.css).
const icons = {
 Home:'<path d="M3.5 11 12 4l8.5 7"/><path d="M5.8 9.3v10.2h12.4V9.3"/><path d="M10 19.5v-5.2h4v5.2"/>',
 Lighting:'<circle cx="12" cy="12" r="4"/><path d="M12 2.5V5M12 19v2.5M2.5 12H5M19 12h2.5M5.3 5.3l1.8 1.8M16.9 16.9l1.8 1.8M5.3 18.7l1.8-1.8M16.9 7.1l1.8-1.8"/>',
 Surfaces:'<path d="M12 3.2c3.4 4 5.8 7.2 5.8 10.3a5.8 5.8 0 0 1-11.6 0c0-3.1 2.4-6.3 5.8-10.3z"/><path d="M9.1 14.1a3 3 0 0 0 2.6 2.6"/>',
 Performance:'<path d="M4.4 17.6a8.6 8.6 0 1 1 15.2 0"/><path d="M12 14.6l4.2-4.9"/><circle cx="12" cy="14.6" r="1.4"/><path d="M6.6 11.6l1.3.6M12 6.4v1.5M17.4 11.6l-1.3.6"/>',
 'DLSS 5':'<path d="M11 3.5l1.9 5.1L18 10.5l-5.1 1.9L11 17.5l-1.9-5.1L4 10.5l5.1-1.9z"/><path d="M18 15l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z"/>',
 Experimental:'<path d="M9.5 3.5h5M10.6 3.5v5.9L5.3 18.1a1.6 1.6 0 0 0 1.4 2.4h10.6a1.6 1.6 0 0 0 1.4-2.4l-5.3-8.7V3.5"/><path d="M7.5 14.6h9"/>',
 Hotkeys:'<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6.5 10h1M10.5 10h1M14.5 10h1M8 14h8"/>',
 Support:'<circle cx="12" cy="12" r="9"/><path d="M9.6 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.3-1.1.8-1.1 1.5v.6"/><path d="M12 16.9v.2"/>',
 Advanced:'<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
 Developer:'<path d="M8.5 7.5 4 12l4.5 4.5M15.5 7.5 20 12l-4.5 4.5M13.5 5.5l-3 13"/>',
 Needs:'<circle cx="12" cy="12" r="9"/><path d="M12 7.5v5.5M12 16.4v.2"/>',
 Ok:'<circle cx="12" cy="12" r="9"/><path d="m8 12.3 2.7 2.7L16 9.6"/>'
};
function icon(name) {const s=el('span','icon');s.setAttribute('aria-hidden','true');s.innerHTML='<svg viewBox="0 0 24 24" focusable="false">'+icons[name]+'</svg>';return s;}
// Pages, in navigation order. Experimental is the release opt-in page; Advanced and Developer exist in developer mode.
const sections = ['Home','Lighting','Surfaces','Performance','DLSS 5','Experimental','Advanced','Developer','Hotkeys','Support'];
const labels = {Support:'Help & support'};
const leads = {
 Home:'Start the game and see what needs attention.',
 Lighting:'Ray-traced shadows, bounced light and hair.',
 Surfaces:'Reflections, water and post effects.',
 Performance:'Frame rate, latency and tracing speed.',
 'DLSS 5':'Neural rendering with your own runtime file.',
 Experimental:'Not fully tested. May change the image or cost performance.',
 Advanced:'Developer controls.',
 Developer:'Developer tools and parked options.',
 Hotkeys:'Keys that work while you play.',
 Support:'Game folder, graphics check and problem reports.'
};
// Where each setting sits on its page: groups in order; ">" marks a child of the row above it (indented, dimmed
// while its parent is off). A setting of the page that is not listed lands in a last group, so none can be lost.
const layout = {
 Lighting:[
  {title:'Ray tracing',lead:'The base the other options build on.',ids:['rt_geometry','rr','rt_intensity']},
  {title:'Shadows',ids:['sun_shadows','local_shadows','>box_lights']},
  {title:'Bounced light',ids:['gi','>exp_hidden_surface']},
  {title:'Hair',ids:['exp_native_coverage','exp_hair_groom_primary','>hair_distant_detail','>hair_motion_stability']}],
 Surfaces:[{title:'Reflections',ids:['reflections']},{title:'Water',ids:['exp_water']},{title:'Post effects',ids:['rr_post_temporal']}],
 Performance:[
  {title:'Frame rate',ids:['frame_generation','reflex','fps_cap']},
  {title:'Tracing speed',lead:'Same image, less GPU time. On in every preset; turn off only to troubleshoot.',ids:['exp_gi_rt_pipeline','>exp_gi_rt_pipeline_reorder'],collapsed:true}],
 'DLSS 5':[{title:'DLSS 5',lead:'Off in every preset.',ids:['dlss5_nr']}],
 Experimental:[{title:'Performance',ids:['scene_handout']},{title:'Frame Generation',ids:['fg_destroy_wait_exact','fg_present_metering']},
  {title:'Water',ids:['water_surface_fresnel']},{title:'RTX Hair',ids:['exp_hair_strand_aa']}],
 Support:[{title:'Problem reports',ids:['detailed_logging']}]
};
const betaIds=['exp_hair_groom_primary'];
// In-game keys (common/hotkeys.h ids; the bridge sends every binding it knows in state.shortcuts).
const hotkeyInfo={
 hotkey_rt_toggle:['All ray tracing off/on',"Compares with the game's own lighting. DLSS and Frame Generation stay as they are."],
 hotkey_dlss5:['DLSS 5 off/on','Works when DLSS 5 was On at game start.'],
 hotkey_rtx_hair:['RTX Hair off/on','Switches between strands and the game\'s own hair while you play.']
};
const hotkeyFor={dlss5_nr:'hotkey_dlss5',exp_hair_groom_primary:'hotkey_rtx_hair'};
const hotkeyName=id=>(hotkeyInfo[id]||[id])[0];
function openSection(id,focus,select) {section=id;if(select)selected[id]=select;render(state);$('main').scrollTop=0;if(focus)$(focus)?.focus?.();}
function el(tag, className, text) { const e=document.createElement(tag);if(className)e.className=className;if(text!==undefined)e.textContent=text;return e; }
function send(type, extra={}) { if(sample) { $('changes').className='';$('status').textContent='Sample preview: actions do not change settings or launch the game';return; } window.chrome?.webview?.postMessage({type,...extra}); }
function measured(text) {return !!text && !/unknown|not measured|not detected|unavailable/i.test(text);}
function pill(text,kind) {return el('span','pill '+(kind||''),text);}
function badge(text,kind,title) {const b=el('span','badge'+(kind?' '+kind:''),text);if(title)b.title=title;return b;}
function linkButton(id,text,fn) {const b=el('button','btn link-btn',text);b.id=id;b.onclick=fn;return b;}
function showDialog(d) {if(!d.open)d.showModal();}
// Keyboard: one Tab stop per group of choices (the current choice); the arrow keys move between the choices and
// Enter or Space chooses. Moving never writes a setting.
function rove(group,vertical) {
 const buttons=[...group.querySelectorAll('button')];
 const current=buttons.find(b=>/(^| )(on|base)( |$)/.test(b.className||'')&&!b.disabled)||buttons.find(b=>!b.disabled);
 buttons.forEach(b=>{b.tabIndex=b===current?0:-1;
  b.onkeydown=e=>{const step=(vertical?{ArrowDown:1,ArrowUp:-1}:{ArrowRight:1,ArrowDown:1,ArrowLeft:-1,ArrowUp:-1})[e.key]||(e.key==='Home'?'first':e.key==='End'?'last':0);if(!step)return;e.preventDefault();
   const live=buttons.filter(x=>!x.disabled),next=step==='first'?live[0]:step==='last'?live[live.length-1]:live[(live.indexOf(b)+step+live.length)%live.length];if(next)next.focus();};});
}
const releaseOptIn=()=>!!state&&!state.developer&&!!state.experimental;
const awaiting=ch=>/experimental|awaiting/.test(ch.maturity||'');
// A release setting whose every choice above the first is unqualified belongs on the Experimental page (Low-latency
// scene). DLSS 5 keeps its own page.
const experimentalOnly=s=>s.id!=='dlss5_nr'&&(s.choices||[]).length>1&&s.choices.slice(1).every(awaiting);
function pageOf(s) {
 if(state.developer)return s.developer?'Developer':s.category;
 return s.category==='Experimental'||experimentalOnly(s)?'Experimental':s.category;
}
const onExperimentalPage=s=>!state.developer&&pageOf(s)==='Experimental';
// Release copy drops the metadata's own "Experimental:" and "(unqualified)" markers: the badge says it.
const plainLabel=(s,text)=>state.developer?text:text.replace(/\s*\((?:unqualified|experimental|advanced)\)/gi,'');
const plainDescription=(s,text)=>state.developer?text:text.replace(/^\(?(?:Experimental|Beta)\)?:?\s*/i,'').replace(/^./,x=>x.toUpperCase());
const settingName=s=>plainLabel(s,s.title||s.label||s.id);
// The one apply badge: amber, only when a change from the current value waits for a game restart. It names the
// direction when only some choices need one ("Restart to turn on", "Restart for Off"). No badge: applies while playing.
function restartBadge(s) {
 const choices=s.choices||[],others=choices.filter(ch=>ch.value!==s.value),need=others.filter(ch=>/restart/i.test(ch.apply||''));
 if(!choices.length)return /restart/i.test(s.apply||'')?badge('Restart','restart',s.apply):null;
 if(!need.length)return null;
 const title=need.map(ch=>plainLabel(s,ch.label)+': '+ch.apply).join('\n');
 if(need.length===others.length)return badge(choices.length===2?'Restart to turn '+(s.value===choices[0].value?'on':'off'):'Restart','restart',title);
 return badge('Restart for '+need.map(ch=>plainLabel(s,ch.label)).join(', '),'restart',title);
}
// Measured costs written at the end of a description move to the cost chip.
function splitCost(text) {const m=/^(.*?)\s*((?:About|Around) \d[\d.]*(?:[–-]\d[\d.]*)? ms\b.*)$/.exec(text||'');return m?[m[1],m[2]]:[text||'',''];}
function costText(s) {
 const cost=String(s.cost||''),estimate=/^estimate/i.test(cost);
 const fromTable=measured(cost)?cost.replace(/^[A-Za-z]+:\s*/,'').replace(/\babout /gi,'\u2248 ').replace(/^./,x=>x.toLowerCase())+(estimate?' (estimate)':''):'';
 const fromText=splitCost(s.description||'')[1].replace(/^(About|Around) /,'\u2248 ').replace(/ on an RTX 5090.*$/,'');
 return fromTable||fromText;
}
// Unmet requirements only, once per row: "Needs X" or "Rough needs X".
function needsText(s) {
 const choices=(s.choices||[]),byReason=new Map();
 choices.forEach(ch=>{if(ch.reason){if(!byReason.has(ch.reason))byReason.set(ch.reason,[]);byReason.get(ch.reason).push(plainLabel(s,ch.label));}});
 const upgrades=choices.slice(1).length;
 return [...byReason].map(([reason,names])=>{
  if(!/^Needs /.test(reason)||names.length>=upgrades||choices.length<=2)return reason;
  return names.join(', ').replace(/, ([^,]*)$/,' and $1')+(names.length>1?' need ':' needs ')+reason.slice(6);
 }).join('\n');
}
// Full requirements for the detail panel, grouped by identical lists.
function requirementLines(s) {
 const groups=new Map();
 (s.choices||[]).forEach(ch=>{if(ch.requirements){if(!groups.has(ch.requirements))groups.set(ch.requirements,[]);groups.get(ch.requirements).push(plainLabel(s,ch.label));}});
 return [...groups].map(([req,names])=>names.join(', ')+': '+req);
}
function choiceControl(s,control,describedBy) {
 const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-label',settingName(s));
 s.choices.forEach(ch=>{const text=plainLabel(s,ch.label),on=ch.value===s.value,b=el('button',on?'on':'',text);b.disabled=ch.enabled===false;b.title=[ch.apply,ch.reason].filter(Boolean).join('. ');b.setAttribute('aria-label',text+'; '+ch.apply);b.setAttribute('aria-pressed',on?'true':'false');b.onclick=()=>send('set',{id:s.id,value:ch.value});seg.append(b);});
 control.className+=' choices';
 if(s.value<0)control.append(el('span','help-copy','Custom value kept'));
 control.append(seg);rove(seg);
}
// The control of one setting and its unmet-requirements line. Ids are control-<id>-<n>.
function settingControl(s) {
 const needs=el('p','needs');needs.id='needs-'+s.id;needs.append(icon('Needs'),el('span','',needsText(s)));needs.hidden=!needsText(s);
 const control=el('div','control'),choices=s.choices||[],name=settingName(s);
 if(choices.length===2&&choices[0].label==='Off'&&plainLabel(s,choices[1].label)==='On') {
  const l=el('label','toggle'),i=el('input');i.type='checkbox';i.setAttribute('role','switch');i.checked=s.value===choices[1].value;i.disabled=choices[i.checked?0:1].enabled===false;i.setAttribute('aria-label',name);i.title=[choices[i.checked?0:1].apply,choices[i.checked?0:1].reason].filter(Boolean).join('. ');i.onchange=()=>send('set',{id:s.id,value:choices[i.checked?1:0].value});l.append(i,el('span'));control.append(l,el('span','toggle-state',i.checked?'On':'Off'));
 } else if(choices.length) {
  choiceControl(s,control,needs.id);
 } else if(s.min!==undefined&&s.max>s.min) {
  const isOff=s.hasOff&&s.value===0;
  if(s.hasOff){const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-label',name);const off=el('button',isOff?'on':'','Off');off.setAttribute('aria-pressed',String(isOff));off.onclick=()=>send('set',{id:s.id,value:0});const on=el('button',!isOff?'on':'','On');on.setAttribute('aria-pressed',String(!isOff));on.onclick=()=>send('set',{id:s.id,value:isOff?s.min:s.value});seg.append(off,on);control.append(seg);rove(seg);}
  const valueText=value=>String(value)+(s.unit?(s.unit==='%'?'':' ')+s.unit:'');
  // The value follows the thumb; the setting is sent once, when the thumb is released (change), not while dragging.
  const i=el('input');i.type='range';i.min=s.min;i.max=s.max;i.step=s.step||1;i.value=isOff?s.min:s.value;i.disabled=isOff;i.setAttribute('aria-label',name);i.setAttribute('aria-valuetext',isOff?'Off':valueText(s.value));const out=el('output','range-value',isOff?'Off':valueText(s.value));i.oninput=()=>{out.textContent=valueText(i.value);i.setAttribute('aria-valuetext',valueText(i.value));};i.onchange=()=>send('set',{id:s.id,value:Number(i.value)});control.className+=' range';control.append(i,out);
 } else control.append(el('span','help-copy',s.value===-1?'Custom value kept':String(s.value??'Unknown')));
 [...control.querySelectorAll('button,input')].forEach((e,index)=>{e.id='control-'+s.id+'-'+index;if(e.type==='checkbox')e.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();e.click();}};e.setAttribute('aria-describedby',needs.id);});
 return [needs,control];
}
// "In game Ctrl+Shift+F8 · Change" on a setting that has a hotkey; only when the bridge knows the binding.
function hotkeyLine(knob,where) {
 const id=hotkeyFor[knob],value=state.shortcuts?.[id];if(!id||value===undefined)return null;
 const p=el('span','hotkey-line');p.id=where+'-hotkey-'+knob;
 p.append(el('span','',value==='None'?'No in-game key':'In game'),...(value==='None'?[]:[el('kbd','',value)]));
 const change=linkButton(where+'-hotkey-change-'+knob,'Change',()=>openSection('Hotkeys',id));change.setAttribute('aria-label','Change the '+hotkeyName(id)+' hotkey');change.setAttribute('aria-describedby',p.id);
 p.append(change);return p;
}
// Detail of one setting, always the same headings: What it does, GPU cost, Takes effect, Needs, presets, key.
function detailNodes(s) {
 const nodes=[],add=(heading,text)=>{if(text)nodes.push(el('p','detail-heading',heading),el('p','detail-copy',text));};
 const [description]=splitCost(plainDescription(s,s.description||s.summary||''));
 add('What it does',[description,s.details].filter(Boolean).join('\n'));
 add('GPU cost',measured(s.cost)||measured(s.vram)?[measured(s.cost)?s.cost:'',measured(s.vram)?'Video memory: '+s.vram:'',s.provenance].filter(Boolean).join('\n'):'Not measured yet.');
 const restart=(s.choices||[]).filter(ch=>ch.value!==s.value&&/restart/i.test(ch.apply||'')).map(ch=>plainLabel(s,ch.label));
 add('Takes effect',[restart.length?(restart.length===(s.choices||[]).length-1?'After a game restart.':'After a game restart: '+restart.join(', ')+'. The other choices apply while you play.'):/restart/i.test(s.apply||'')?'After a game restart.':'At once, also while the game runs.',s.applyNote].filter(Boolean).join('\n'));
 add('Needs',requirementLines(s).join('\n')||(s.reason||''));
 if(s.presets)add('Low · Medium · High · Ultra',s.presets);
 const key=hotkeyFor[s.id],binding=state.shortcuts?.[key];
 if(binding!==undefined)add('In-game key',binding==='None'?'None. Set one on Hotkeys.':binding+' (change it on Hotkeys)');
 return nodes;
}
function renderDetail() {
 const panel=$('detail');panel.replaceChildren();
 const s=(state.settings||[]).find(x=>x.id===selected[section]&&pageOf(x)===section);
 panel.append(el('p','label detail-kicker','Details'));
 if(!s){panel.append(el('p','detail-empty','Select a setting to see what it does, what it costs and when it takes effect.'));return;}
 panel.append(el('h2','detail-title',settingName(s)),...detailNodes(s));
}
function selectRow(id) {
 if(selected[section]===id)return;
 const old=$('card-'+selected[section]);if(old)old.className=old.className.replace(' selected','');
 selected[section]=id;const row=$('card-'+id);if(row)row.className+=' selected';
 renderDetail();
}
// One setting row. Its id stays card-<id> (tests and deep links use it).
function row(s,opts={}) {
 const full=settingName(s),short=opts.child&&opts.parentName&&full.startsWith(opts.parentName+' ')?full.slice(opts.parentName.length+1).replace(/^./,x=>x.toUpperCase()):full;
 const c=el('article','row selectable'+(opts.child?' child':'')+(opts.parentOff?' parent-off':'')+(selected[section]===s.id?' selected':''));c.id='card-'+s.id;
 c.onclick=()=>selectRow(s.id);c.addEventListener?.('focusin',()=>selectRow(s.id));
 const text=el('div','row-text'),head=el('div','row-head'),title=el('h3','row-title',short);if(short!==full)title.setAttribute('aria-label',full);head.append(title);
 if(betaIds.includes(s.id)&&!state.developer)head.append(badge('Beta','beta','Supported, off in every preset.'));
 if(!state.developer&&(onExperimentalPage(s)||s.id==='dlss5_nr'))head.append(badge('Experimental','exp','Not fully tested. Off in every preset.'));
 if(state.developer&&s.maturity==='experimental')head.append(pill('Experimental','exp'));
 if((state.restartNames||[]).includes(s.title))head.append(badge('Restart pending','pending','Saved. Takes effect after a game restart.'));
 text.append(head);
 const [description]=splitCost(plainDescription(s,s.description||s.summary||''));
 text.append(el('p','description',description+(s.developer?' GPU cost: '+(s.cost||'Not measured.'):'')));
 const meta=el('div','row-meta'),rb=restartBadge(s),cost=s.developer?'':costText(s),key=hotkeyLine(s.id,'card');
 if(rb)meta.append(rb);
 if(cost){const chip=el('span','chip',cost);chip.title='Measured on an RTX 5090. Other GPUs differ.';meta.append(chip);}
 if(key)meta.append(key);
 text.append(meta);
 const [needs,control]=settingControl(s);text.append(needs);
 if(opts.parentOff)text.append(el('p','cost','Only has an effect while '+opts.parentName+' is on. Its value is kept.'));
 // What changed with this click, where the click was made (the status strip repeats it on every page).
 const n=state.notice||{};
 if(n.anchor===s.id&&(n.also||[]).length){const note=el('p','also');note.id='row-also';note.setAttribute('role','status');note.append(el('span','','Also changed: '+n.also.join('; ')+'. '));if(n.undo)note.append(linkButton('row-undo','Undo',()=>send('undo')));text.append(note);}
 const side=el('div','row-control');side.append(control);
 c.append(text,side);
 if(s.id==='dlss5_nr')c.append(runtimeFile(s));
 const inline=el('div','row-detail');inline.append(...detailNodes(s));c.append(inline);
 return c;
}
function runtimeFile(s) {
 const runtime=el('div','row-wide runtime-file'),path=el('p','path',state.dlss5RuntimePath||'No file chosen: looking in the game folder');
 path.id='dlss5-runtime-path';runtime.append(el('p','detail-heading','DLSS 5 runtime file'),path,el('p','help-copy',state.dlss5RuntimeStatus||'Your runtime is prepared before Play; it is not included with BG3RTX.'));
 const actions=el('div','option-row'),browse=el('button','btn small','Browse…');browse.id='dlss5-runtime-browse';browse.setAttribute('aria-describedby','needs-'+s.id);browse.onclick=()=>send('browseDlss5Runtime');actions.append(browse);
 if(state.dlss5RuntimePath){const clear=el('button','btn small','Look in the game folder');clear.id='dlss5-runtime-clear';clear.setAttribute('aria-describedby','needs-'+s.id);clear.onclick=()=>send('clearDlss5Runtime');actions.append(clear);}
 runtime.append(actions);return runtime;
}
// A group: small caps heading and one panel. `collapsed` groups are a disclosure that remembers its state.
function group(title,lead,children,opts={}) {
 const box=el('div','rows');box.append(...children);
 if(opts.collapsed){const d=el('details','group'),sum=el('summary');const slug=String(opts.id||title).replace(/[^A-Za-z0-9]+/g,'-');d.id='group-'+slug;sum.id='group-summary-'+slug;sum.append(el('h2','group-title',title));if(lead)sum.append(el('span','group-lead',lead));restoreDisclosure(d,opts.open);d.append(sum,box);return d;}
 const g=el('section','group'),head=el('div','group-head');head.append(el('h2','group-title',title));if(lead)head.append(el('p','group-lead',lead));
 if(opts.id)g.id=opts.id;g.append(head,box);return g;
}
// Rows of one page from its layout: listed settings in order with their children, then anything not listed.
function settingGroups(root,page,items) {
 const byId=new Map(items.map(s=>[s.id,s])),used=new Set(),value=id=>(state.settings||[]).find(s=>s.id===id)?.value;
 (layout[page]||[]).forEach(g=>{
  const rows=[];let parent=null;
  g.ids.forEach(raw=>{const child=raw[0]==='>',id=child?raw.slice(1):raw,s=byId.get(id);if(!child)parent=id;if(!s)return;used.add(id);
   const parentSetting=child?(state.settings||[]).find(x=>x.id===parent):null;
   rows.push(row(s,{child:child&&!!parentSetting,parentOff:child&&!!parentSetting&&value(parent)===0,parentName:parentSetting?settingName(parentSetting):''}));});
  if(rows.length)root.append(group(g.title,g.lead,rows,{collapsed:g.collapsed,open:g.ids.some(id=>selected[page]===id.replace('>',''))}));
 });
 const rest=items.filter(s=>!used.has(s.id));
 if(rest.length)root.append(group(used.size?'More':labels[page]||page,'',rest.map(s=>row(s))));
}
// One line at the end of a page that has experimental options: where they are.
function experimentalPointer(root,page) {
 if(state.developer)return;
 const all=(layout.Experimental||[]).flatMap(g=>g.ids),topic={Lighting:['exp_hair_strand_aa'],Surfaces:['water_surface_fresnel'],Performance:['scene_handout','fg_destroy_wait_exact','fg_present_metering']}[page];
 if(!topic)return;
 const shown=(state.settings||[]).filter(s=>topic.includes(s.id)&&all.includes(s.id));
 if(releaseOptIn()&&!shown.length)return;
 const p=el('div','pointer');p.id='experimental-pointer';
 p.append(icon('Experimental'),el('span','',releaseOptIn()?(shown.length===1?'1 experimental option: ':shown.length+' experimental options: ')+shown.map(settingName).join(', ')+'.':'Experimental options for this page are hidden.'));
 p.append(linkButton('experimental-open','Open Experimental',()=>openSection('Experimental',null,shown[0]?.id)));root.append(p);
}
// Hotkeys page: every in-game key in one place (state.shortcuts, one entry per common/hotkeys.h id). The bridge
// validates and resets; this only captures a combination and blocks an obvious clash before sending it.
function hotkeyRow(id) {
 const [name,note]=hotkeyInfo[id]||[id,''];
 const r=el('div','hotkey-row');r.id='hotkey-row-'+id;
 const label=el('label','shortcut-label'),title=el('span','row-title',name),input=el('input');label.htmlFor=id;
 input.type='text';input.readOnly=true;input.id=id;input.value=state.shortcuts?.[id]||'None';
 input.placeholder='Press keys';input.setAttribute('aria-label',name+': press a key combination');
 const help=el('p','help-copy',note);help.id=id+'-help';input.setAttribute('aria-describedby',help.id);
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
  const clash=Object.entries(state.shortcuts||{}).find(([other,v])=>other!==id&&v===value);
  if(value!=='None'&&((e.ctrlKey&&e.shiftKey&&e.key.toUpperCase()==='F10')||clash)){help.textContent=clash?'Already used by '+hotkeyName(clash[0])+'. Choose another combination.':'Ctrl+Shift+F10 is reserved for a developer tool. Choose another combination.';input.value=value;return;}
  send('shortcut',{id,value});
 };
 const reset=el('button','btn small','Reset to '+(state.shortcutDefaults?.[id]||'default'));reset.id='hotkey-reset-'+id;reset.disabled=!!state.shortcutDefaults&&state.shortcutDefaults[id]===state.shortcuts?.[id];reset.onclick=()=>send('resetShortcut',{id});
 const disable=el('button','btn small','Clear');disable.id='hotkey-none-'+id;disable.setAttribute('aria-label','No key for '+name);disable.onclick=()=>send('shortcut',{id,value:'None'});
 [reset,disable].forEach(b=>b.setAttribute('aria-describedby',help.id));
 const actions=el('div','hotkey-actions');actions.append(input,reset,disable);
 label.append(title);const text=el('div');text.append(label,help);r.append(text,actions);
 return r;
}
function renderHotkeys(root) {
 const rows=Object.keys(state.shortcuts||{}).map(hotkeyRow);
 const g=group('In-game keys','',rows,{id:'hotkeys'});
 g.append(el('p','cost','Select a field and press a combination with Ctrl, Shift or Alt and F1-F12, A-Z or 0-9. Delete or Backspace clears it, Escape cancels.'));
 root.append(g);
}
// Preparation (docs/RTX_HAIR_ROADMAP.md 2b, 2f; tools/launcher/preparation.cpp): the host sends
// state.preparation = {items:[{id,title,line,detail,progress?,state}], whilePlaying, paused, pausable, convertAll,
// hair:{ready?,captured?,total?,needed?,finished?,bytes?,etaSeconds?,source?,error?,tiers?:[{tier,total,finished}]}, granny:{path,ok},
// grannyHelp?, convertAllReason}.
// Every field may be missing (older hosts and converters): the panel shows what it has. Actions apply at once
// ('prep' messages): start, pause, resume, convertAll 0/1, browseGranny, clearGranny, whilePlaying 0/1.
const prepBadges={done:['Ready','ok'],running:['Working','work'],waiting:['Waiting','idle'],paused:['Paused','idle'],unavailable:['Unavailable','idle'],failed:['Stopped','stopped']};
function stateBadge(p) {const [text,kind]=prepBadges[p.state]||[p.state||'','idle'];const b=badge(text,kind);b.id='prep-state-'+p.id;return b;}
const minutes=s=>s>=5400?'about '+Math.round(s/3600)+' h left':s>=60?'about '+Math.round(s/60)+' min left':'less than a minute left';
const sizeText=b=>b>=1e8?(b/1e9).toFixed(1)+' GB':Math.round(b/1e6)+' MB';
function progressBar(p,label,eta) {
 const wrap=el('div','progress-row');
 const bar=el('progress');bar.max=1;bar.value=p.progress;bar.id='prep-progress-'+p.id;bar.setAttribute('aria-label',label);
 const pct=Math.round(p.progress*100)+'%';
 const text=el('span','progress-text',pct+(eta?' · '+eta:''));bar.setAttribute('aria-valuetext',text.textContent);
 wrap.append(bar,text);return wrap;
}
function stat(id,label,value) {const s=el('div','stat');s.id='hair-stat-'+id;s.append(el('span','stat-value',value),el('span','stat-label',label));return s;}
const working=p=>!!p&&(p.state==='running'||p.state==='waiting')&&typeof p.progress==='number';
// RTX Hair style conversion: numbers, progress, one action; source, granny2.dll and schedule under "Conversion options".
function hairJob(prep) {
 const items=prep.items||[],p=items.find(i=>i.id==='hair'),h=prep.hair||{};
 const box=el('div','job');box.id='home-hair';
 if(!p)return null;
 const head=el('div','job-head'),text=el('div');text.append(el('h3','row-title','RTX Hair styles'),el('p','description',p.line||''));head.append(text,stateBadge(p));box.append(head);
 const body=el('div','prep-item');body.id='prep-hair';
 const num=v=>typeof v==='number'&&v>=0?v.toLocaleString('en-US'):'–';
 // The tiles carry the numbers when the host sends them.
 if(['ready','captured','total','bytes'].some(k=>typeof h[k]==='number')){const stats=el('div','stats');stats.append(stat('ready','Ready',num(h.ready)),stat('captured','Seen in game',num(h.captured)),stat('total','In the game',num(h.total)),stat('disk','On disk',typeof h.bytes==='number'?sizeText(h.bytes):'–'));body.append(stats);}
 if(typeof p.progress==='number'&&p.state!=='done'&&p.state!=='unavailable')body.append(progressBar(p,'RTX Hair conversion progress',p.state==='running'&&h.etaSeconds>0?minutes(h.etaSeconds):''));
 if(p.detail)body.append(el('p','cost',p.detail));
 // Convert all styles: progress per tier, in conversion order (bg3rtx_hairconv --all).
 if(Array.isArray(h.tiers)&&h.tiers.length){const names={seen:'Seen in game',companion:'Companions',player:'Character creation',named:'Named characters',hat:'Under hats',other:'Other characters',unused:'Unused'};
  const t=el('p','cost',h.tiers.map(x=>(names[x.tier]||x.tier)+' '+num(x.finished)+'/'+num(x.total)).join(' · '));t.id='prep-hair-tiers';body.append(t);}
 if(h.error){const e=el('p','notice error compact','Last error: '+h.error);e.id='prep-hair-error';e.setAttribute('role','alert');body.append(e);}
 box.append(body);
 if(p.state==='unavailable')return box;
 const action=prep.paused?['resume','Resume']:p.state==='failed'||p.state==='done'?['start',p.state==='failed'?'Try again':'Convert again']:['pause','Pause'];
 const go=el('button','btn small',action[1]);go.id='prep-pause';go.onclick=()=>send('prep',{action:action[0],value:0});
 go.disabled=action[0]==='pause'&&prep.pausable===false;head.append(go);
 const opts=el('details','options'),sum=el('summary','','Conversion options');opts.id='prep-options';sum.id='prep-options-summary';restoreDisclosure(opts,p.state==='failed'||!!h.error);opts.append(sum);
 // Source: styles the game showed (captured by the layer) or every style in the paks (needs the player's granny2.dll).
 const src=el('div','option-group'),srcLabel=el('span','option-label','Convert');srcLabel.id='prep-source-label';
 const seg=el('div','seg');seg.setAttribute('role','group');seg.setAttribute('aria-labelledby',srcLabel.id);
 const reason=prep.convertAllReason||'',all=!!prep.convertAll;
 const why=el('p','needs');why.id='prep-source-reason';why.append(icon('Needs'),el('span','',reason));why.hidden=!reason||all;
 [['Styles seen in game',0],['All styles',1]].forEach(([label,value])=>{const on=all===!!value,b=el('button',on?'on':'',label);b.id='prep-source-'+value;b.setAttribute('aria-pressed',String(on));b.setAttribute('aria-describedby',why.id);
  if(value&&reason&&!all){b.disabled=true;b.title=reason;}b.onclick=()=>send('prep',{action:'convertAll',value});seg.append(b);});
 rove(seg);const srcRow=el('div','option-row');srcRow.append(seg);src.append(srcLabel,srcRow,why);
 // granny2.dll: the player's own copy; never shipped.
 const g=prep.granny||{},dll=el('div','granny');dll.id='prep-granny';
 const path=el('p','path',g.path?g.path:'granny2.dll: not chosen');path.id='prep-granny-path';
 if(g.path&&g.ok===false)path.append(el('span','warn-text',' (not found)'));
 const pick=el('button','btn small',g.path?'Change…':'Choose granny2.dll…');pick.id='prep-granny-browse';pick.setAttribute('aria-label',(g.path?'Change':'Choose')+' granny2.dll');pick.onclick=()=>send('prep',{action:'browseGranny',value:0});
 const help=el('p','cost',prep.grannyHelp||'Convert all styles needs granny2.dll, which BG3RTX cannot include. If you have LSLib\'s ExportTool (github.com/Norbyte/lslib, Releases), point to the granny2.dll in its folder.');help.id='prep-granny-help';
 const prow=el('div','option-row');prow.append(pick);
 if(g.path){const clear=el('button','btn small','Clear');clear.id='prep-granny-clear';clear.setAttribute('aria-label','Clear granny2.dll');clear.onclick=()=>send('prep',{action:'clearGranny',value:0});prow.append(clear);}
 [...prow.children].forEach(b=>b.setAttribute('aria-describedby',help.id));
 dll.append(path,prow,help);src.append(dll);opts.append(src);
 const when=el('div','option-group'),whenLabel=el('span','option-label','When to convert');whenLabel.id='prep-when-label';
 const ws=el('div','seg prep-when');ws.setAttribute('role','group');ws.setAttribute('aria-labelledby',whenLabel.id);
 [['Game closed',0],['Also while playing',1]].forEach(([label,value])=>{const on=!!prep.whilePlaying===!!value,b=el('button',on?'on':'',label);b.id='prep-when-'+value;b.setAttribute('aria-pressed',on?'true':'false');b.onclick=()=>send('prep',{action:'whilePlaying',value});ws.append(b);});
 rove(ws);const whenRow=el('div','option-row');whenRow.append(ws);when.append(whenLabel,whenRow,el('p','cost','While playing, it uses one core at low priority.'));opts.append(when);
 box.append(opts);
 return box;
}
function shaderJob(prep) {
 const box=el('div','job');box.id='home-shaders';
 const p=(prep.items||[]).find(i=>i.id==='shaders');
 const head=el('div','job-head'),text=el('div');text.append(el('h3','row-title','Shaders'),el('p','description',p?p.line||'':'The shader status appears here after the launcher has checked the mod files.'));head.append(text);if(p)head.append(stateBadge(p));box.append(head);
 if(!p)return box;
 const body=el('div','prep-item');body.id='prep-shaders';
 if(typeof p.progress==='number'&&p.state!=='done'&&p.state!=='unavailable')body.append(progressBar(p,'Shader preparation progress',''));
 if(p.detail)body.append(el('p','cost',p.detail));
 box.append(body);return box;
}
function presetButtons(root) {
 root.replaceChildren();
 ['Low','Medium','High','Ultra'].forEach((name,index)=>{const on=state.preset===index,b=el('button',on?(state.custom?'base':'on'):'',name);b.id='preset-'+index;b.setAttribute('aria-pressed',on&&!state.custom?'true':'false');b.title=[state.presetDescriptions?.[index],index===state.recommendedPreset?'Suggested for this GPU; performance varies.':''].filter(Boolean).join('\n');b.onclick=()=>send('preset',{value:index});root.append(b);});
 rove(root);
 const note=$('preset-note');note.replaceChildren();
 const name=['Low','Medium','High','Ultra'][state.preset]||'';
 if(state.custom){const c=el('span','custom-preset','Custom');c.setAttribute('aria-label','Custom settings');note.append(c,el('span','',', based on '+name+' · '),linkButton('preset-reset','Reset to '+name,()=>send('preset',{value:state.preset})));}
 else if(state.preset===state.recommendedPreset)note.append(el('span','','Suggested for this GPU'));
}
// Home: only what needs attention. One line per problem or pending work, each with its fix where the launcher has one.
function statusPanel() {
 const list=el('ul','status-list');list.id='home-attention';list.setAttribute('aria-label','Needs attention');
 const item=(id,kind,text,button)=>{const li=el('li',kind);li.id='home-status-'+id;li.append(icon(kind==='ok'?'Ok':'Needs'),el('span','status-text',text));if(button)li.append(button);list.append(li);return li;};
 const btn=(id,text,fn,primary)=>{const b=el('button','btn small'+(primary?' primary':''),text);b.id=id;b.onclick=fn;return b;};
 const c=state.compatibility||{},prep=state.preparation||{};
 if(state.gamePath==='')item('game','err',"Baldur's Gate 3 was not found. Choose the folder that contains bin\\bg3.exe. You can still look through the settings.",btn('home-browse','Choose folder…',()=>send('browseGame'),true));
 const warn=(c.warnings||[]).join(' '),known=measured(c.gpu||'')&&!/not (chosen|detected)/i.test(c.gpu||'');
 if(warn||!known)item('gpu','warn',warn||'The graphics card could not be checked.',btn('home-recheck','Check again',()=>send('recheck')));
 const mods=state.modStatus||'';if(/missing/i.test(mods))item('mods','err','Mod files are missing. Install BG3RTX again.');
 if(state.writeError?.text)item('error','err',state.writeError.text,state.writeError.retry?btn('home-retry','Retry',()=>send('retry')):null);
 if(state.restartNeeded)item('restart','warn',(state.restartBanner||'Restart the game to apply saved changes').replace(/\.$/,'')+'.');
 const hair=(prep.items||[]).find(i=>i.id==='hair'),shaders=(prep.items||[]).find(i=>i.id==='shaders');
 if(hair?.state==='failed')item('hair','warn','RTX Hair style conversion stopped. See Preparation below.');
 else if(working(hair)||working(shaders))item('prep','info',(working(hair)?'RTX Hair styles are still converting':'Shaders are still being prepared')+'. You can play now.');
 if(!list.children.length)item('none','ok','Nothing needs attention.');
 return list;
}
// In-game keys at a glance; the Hotkeys page changes them.
function keysPanel() {
 const ids=Object.keys(state.shortcuts||{});if(!ids.length)return null;
 const list=el('dl','key-list');
 ids.forEach(id=>{const r=el('div');const v=state.shortcuts[id];r.append(el('dt','',hotkeyName(id)),(()=>{const dd=el('dd');dd.append(v==='None'?el('span','help-copy','None'):el('kbd','',v));return dd;})());list.append(r);});
 const pad=el('div','pad');pad.append(list,linkButton('home-keys-change','Change keys',()=>openSection('Hotkeys')));
 const g=group('In-game keys','',[pad],{id:'home-keys'});return g;
}
const gpuLine=()=>[state.compatibility?.gpu,measured(state.compatibility?.vram)?state.compatibility.vram:null,measured(state.compatibility?.driver)&&!/not detected/i.test(state.compatibility?.driver||'')?state.compatibility.driver:null].filter(Boolean).join(' · ')||'GPU not detected';
function renderHome(root) {
 const blocked=state.gamePath==='',presetName=(state.custom?'Custom, based on ':'')+(['Low','Medium','High','Ultra'][state.preset]||'');
 const hero=el('div','hero'),mark=el('div','hero-icon'+(blocked?' bad':'')),words=el('div');hero.id='home-play-card';
 mark.append(icon(blocked?'Needs':'Ok'));
 const facts=el('p','',[presetName,gpuLine()].filter(Boolean).join(' · '));facts.id='home-status';
 words.append(el('h2','',blocked?'Game folder needed':state.canPlay?'Ready to play':'The game is running'),facts);hero.append(mark,words);root.append(hero);
 const grid=el('div','home'),left=el('div'),right=el('div');
 const attention=el('section','group'),ah=el('div','group-head');ah.append(el('h2','group-title','Needs attention'));
 const report=el('p','help-copy home-report');report.id='home-report';report.append(el('span','','Problem? '),linkButton('home-report-open','Create a report…',()=>send('report')));
 attention.append(ah,statusPanel(),report);left.append(attention);
 const prep=state.preparation||{},jobs=[shaderJob(prep),hairJob(prep)].filter(Boolean);
 left.append(group('Preparation','Runs in the background. Never blocks Play.',jobs,{id:'home-preparation'}));
 const index=state.preset,pp=el('div','pad');pp.append(el('h3','row-title',['Low','Medium','High','Ultra'][index]||'Preset'),el('p','description',state.presetDescriptions?.[index]||''));
 if(state.custom)pp.append(el('p','cost','You changed some settings from this preset. "Reset to '+(['Low','Medium','High','Ultra'][index]||'preset')+'" at the top right brings it back.'));
 right.append(group('This preset','',[pp],{id:'home-preset'}));
 const keys=keysPanel();if(keys)right.append(keys);
 grid.append(left,right);root.append(grid);
}
function renderSupport(root) {
 const setup=el('div','row');setup.id='support-setup';
 const st=el('div','row-text');st.append(el('h3','row-title','Game folder'),el('p','path'+(state.gamePath?'':' warn-text'),state.gamePath||'Not found. Choose the folder that contains bin\\bg3.exe.'),el('p','description','Play starts the Vulkan version of the game (bin\\bg3.exe) with BG3RTX.'));
 const browse=el('button','btn','Choose folder…');browse.id='support-browse';browse.onclick=()=>send('browseGame');const sc=el('div','row-control');sc.append(browse);setup.append(st,sc);
 const memory=state.compatibility||{},gfx=el('div','row');gfx.id='support-graphics';
 const warn=(memory.warnings||[]).length>0,gt=el('div','row-text'),gh=el('div','row-head');gh.append(el('h3','row-title','Graphics'),badge(warn?'Check needed':'Supported',warn?'restart':'ok'));
 gt.append(gh,el('p','detail-heading','Compatibility'),el('p','description pre',[memory.gpu,...(memory.details||[]).filter(x=>x&&x!==memory.memory)].filter(Boolean).join('\n')),
  el('p','detail-heading','Video memory'),el('p','description pre',memory.memory||(measured(memory.vram)?memory.vram:'Not checked yet. Use Check again.')));
 const recheck=el('button','btn','Check again');recheck.id='support-recheck';recheck.onclick=()=>send('recheck');const gc=el('div','row-control');
 if((memory.adapters||[]).length>1){const wrap=el('span','select-wrap'),select=el('select');select.id='support-adapter';select.setAttribute('aria-label','GPU');const unknown=el('option','','Choose GPU');unknown.value='-1';select.append(unknown);memory.adapters.forEach(a=>{const option=el('option','',a.label);option.value=a.value;option.selected=a.value===memory.selected;select.append(option);});select.onchange=()=>send('adapter',{value:Number(select.value)});wrap.append(select);gc.append(wrap);}
 gc.append(recheck);gfx.append(gt,gc);
 root.append(group('Game and graphics','',[setup,gfx],{id:'support-game'}));
 const rep=el('div','row'),rt=el('div','row-text');rt.append(el('h3','row-title','Report a problem'),el('p','description','Builds a ZIP on your PC with logs, settings and graphics details. You see every file first. Nothing is uploaded.'));
 const b=el('button','btn primary','Create a report…');b.id='support-report';b.onclick=()=>send('report');const rc=el('div','row-control');rc.append(b);rep.append(rt,rc);
 const support=(state.settings||[]).filter(s=>pageOf(s)==='Support');
 root.append(group('Problem reports','',[rep,...support.map(s=>row(s))],{id:'support-reports'}));
 const rtKey=state.shortcuts?.hotkey_rt_toggle||'Ctrl+Shift+F9',tips=el('ul','tips');
 ['Game does not start, or Frame Generation does nothing: turn off other overlays (Steam, Discord, GOG Galaxy) for one launch.',
  (rtKey==='None'?'No key is set to switch':'In game, '+rtKey+' switches')+" all ray-traced lighting off and on, to compare with the game's own image. Change it on Hotkeys. DLSS and Frame Generation stay as they are.",
  "DLSS Ray Reconstruction needs DLSS as the upscaler in the game's graphics options. Play sets it for you."].forEach(t=>tips.append(el('li','',t)));
 root.append(group('Tips','',[tips],{id:'support-tips'}));
 const about=el('div','row'),at=el('div','row-text');about.id='support-about';
 at.append(el('h3','row-title','BG3RTX '+(state.version||'')),el('p','description',(state.modStatus||'Mod files not checked')+". Independent fan mod; not affiliated with the game's developers."));
 const notices=el('a','link','Fonts and third-party notices');notices.href='https://app.bg3rtx.local/notices';notices.onclick=e=>{e.preventDefault();send('notices');};
 const copy=el('button','btn small','Copy version');copy.id='support-copy-version';copy.onclick=()=>{try{navigator.clipboard.writeText('BG3RTX '+(state.version||''));copy.textContent='Copied';}catch(e){copy.textContent=state.version||'';}};
 const ac=el('div','row-control');ac.append(copy,notices);about.append(at,ac);
 root.append(group('About','',[about],{id:'support-about-group'}));
}
function renderExperimental(root,items) {
 // Release opt-in: the switch sits on the page, above the options it reveals.
 const box=el('div','exp-switch'),text=el('div'),l=el('label','toggle'),i=el('input');
 text.append(el('h3','row-title','Show experimental options'),el('p','description','Not fully tested. Off in every preset. Turning this off hides them and sets them all back to Off.'));
 i.type='checkbox';i.setAttribute('role','switch');i.id='experimental-switch';i.checked=releaseOptIn();i.setAttribute('aria-label','Show experimental options');
 i.onchange=()=>send('experimental',{value:i.checked});i.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();i.click();}};
 l.append(i,el('span'));const ctl=el('div','control');ctl.append(l,el('span','toggle-state',i.checked?'On':'Off'));box.append(text,ctl);root.append(box);
 if(!releaseOptIn()){const e=el('p','empty','Experimental options are hidden and off. Turn the switch on to see them.');e.id='experimental-empty';root.append(e);return;}
 settingGroups(root,'Experimental',items);
}
function renderSettings() {
 const root=$('section-content');rememberDisclosures(root);root.replaceChildren();
 const noDetail=['Home','Hotkeys','Support'].includes(section);
 $('body-grid').className='body'+(noDetail?' no-detail':'');
 if(section==='Home'){renderHome(root);return;}
 if(section==='Hotkeys'){renderHotkeys(root);return;}
 if(section==='Support'){renderSupport(root);return;}
 const all=(state.settings||[]).filter(s=>pageOf(s)===section);
 const order=[...(layout[section]||[]).flatMap(g=>g.ids.map(id=>id.replace('>',''))),...all.map(s=>s.id)];
 if(!selected[section]||!all.some(s=>s.id===selected[section]))selected[section]=order.find(id=>all.some(s=>s.id===id));
 if(section==='Experimental'&&!state.developer){renderExperimental(root,all);renderDetail();return;}
 if(state.developer) {
  // Developer mode: one disclosure authorizes experimental choices; policy remains in the bridge.
  const isExperimental=s=>s.id!=='dlss5_nr'&&(s.experimental===true||(s.experimental===undefined&&s.maturity==='experimental'));
  const regular=all.filter(s=>!isExperimental(s)),experimental=all.filter(isExperimental);
  settingGroups(root,section,regular);
  if(experimental.length||section==='Advanced') {
   const d=el('details','group exp');d.open=experimentalOpen;const summary=el('summary');summary.id='experimental-summary';summary.append(pill('Experimental','exp'),el('span','group-lead','Unqualified features. Cost and output may vary.'));d.append(summary);const g=el('div','rows');experimental.forEach(s=>g.append(row(s)));if(!experimental.length)g.append(el('p','help-copy pad','Experimental choices are available on their feature rows when expanded.'));d.append(g);d.ontoggle=()=>{if(experimentalOpen!==d.open){experimentalOpen=d.open;send('experimental',{value:d.open});}};root.append(d);
  }
 } else {settingGroups(root,section,all);experimentalPointer(root,section);}
 renderDetail();
}
const hasAdvanced=()=>!!state.developer||(state.settings||[]).some(s=>s.category==='Advanced'&&!s.developer);
function visibleSections() {
 return sections.filter(id=>id==='Experimental'?!state.developer:id==='Advanced'?hasAdvanced():id==='Developer'?!!state.developer&&(state.settings||[]).some(s=>s.developer):id==='Hotkeys'?Object.keys(state.shortcuts||{}).length>0:true);
}
function renderNavigation() {
 const nav=$('navigation');nav.replaceChildren();
 const prep=state.preparation||{},busy=(prep.items||[]).some(working);
 visibleSections().forEach(id=>{
  if(id==='Hotkeys'||(id==='Support'&&!visibleSections().includes('Hotkeys')))nav.append(el('div','nav-sep'));
  const b=el('button',id===section?'on':'');b.id='nav-'+id;b.setAttribute('aria-current',id===section?'page':'false');b.append(icon(id),el('span','nav-label',labels[id]||id));
  if(id==='Home'&&state.gamePath===''){const n=el('span','nav-count','1');n.title='1 problem';b.append(n);}
  else if(id==='Home'&&busy){const d=el('span','nav-dot');d.title='Preparation is running';b.append(d);}
  b.onclick=()=>openSection(id);nav.append(b);});
 rove(nav,true);
 if(sample)nav.append(el('div','sample-label','Sample data · no game launch'));
}
function render(next) {
 const focused=document.activeElement?.id;
 const focusedDialog=['report-dialog'].find(id=>$(id).open&&$(id).contains(document.activeElement));
 state=next;if(state.experimental!==undefined)experimentalOpen=state.experimental;
 if(!visibleSections().includes(section))section=section==='Advanced'&&visibleSections().includes('Hotkeys')?'Hotkeys':'Lighting';
 $('report-progress').hidden=true;$('cancel-report').hidden=true;$('prepare-report').disabled=false;
 // Same shell on every page: title and preset in the header, Play and the version at the bottom of the sidebar.
 $('page-title').textContent=labels[section]||section;$('page-lead').textContent=leads[section]||'';
 presetButtons($('presets'));
 renderNavigation();
 $('hero-version').textContent='BG3RTX '+(state.version||'version unavailable');
 const warnings=$('warnings');warnings.replaceChildren();
 if(state.gamePath===''&&section!=='Home'){const n=el('div','notice setup'),b=el('button','btn small primary','Choose folder…');n.setAttribute('role','alert');b.id='notice-browse';b.onclick=()=>send('browseGame');n.append(icon('Needs'),el('span','',"Baldur's Gate 3 was not found. Choose the folder that contains bin\\bg3.exe to turn Play on."),b);warnings.append(n);}
 // A failed write: nothing changed; the page keeps showing what is in the file. Retry repeats the same write.
 if(state.writeError?.text){const n=el('div','notice error');n.id='write-error';n.setAttribute('role','alert');n.append(icon('Needs'),el('span','',state.writeError.text));
  if(state.writeError.retry){const b=el('button','btn small primary','Retry');b.id='retry-write';b.onclick=()=>send('retry');n.append(b);}
  const x=el('button','btn small','Dismiss');x.id='dismiss-error';x.onclick=()=>send('dismiss');n.append(x);warnings.append(n);}
 if(state.restartNeeded){const n=el('div','notice restart');n.id='restart-banner';n.setAttribute('role','status');n.append(icon('Needs'),el('span','',state.restartBanner||'Restart the game to apply saved changes.'));warnings.append(n);}
 renderFooter();
 $('play').disabled=!state.canPlay;
 $('play-note').textContent=state.canPlay?'':state.gamePath===''?'Choose the game folder first':'Close the running game to play again';
 renderSettings();renderReport();
 if(focused){const target=$(focused);if(target&&((focusedDialog&&$(focusedDialog).open&&$(focusedDialog).contains(target))||!$('report-dialog').open))target.focus({preventScroll:true});}
}
// Status strip: the last change (with what changed with it and Undo) or a message from the bridge. Hidden otherwise.
function renderFooter() {
 const n=state.notice||{},also=(n.also||[]).length?' Also changed: '+n.also.join('; ')+'.':'';
 const text=n.text?n.text+'.'+also:state.status||'';
 $('changes').className=text?'':'idle';$('status').textContent=text;
 $('undo').hidden=!n.undo;$('dismiss').hidden=!n.text;
}
function renderReport() {
 const r=state.report,d=$('report-dialog');if(!r?.visible){if(d.open)d.close();return;}$('logging-hint').textContent=r.loggingHint||'';$('logging-hint').hidden=!r.loggingHint;$('privacy').textContent=r.privacy||'Saves selected logs, settings and graphics details as a ZIP on your PC. Personal paths are partly removed; review every file before sharing. Nothing is uploaded. Screenshots and crash dumps are excluded; your latest save is added only if you tick it.';
 const root=$('report-preview'),notice=state.status&&/report|package/i.test(state.status)?state.status:'';
 const signature=JSON.stringify([r,notice]);if(signature===reportSignature){showDialog(d);return;}reportSignature=signature;rememberDisclosures(root);root.replaceChildren();if(r.message)root.append(el('p','help-copy',r.message));if(state.status&&/report|package/i.test(state.status))root.append(el('p','notice',state.status));const files=r.files||[];if(files.length){root.append(el('h3','','Preview · '+files.length+' files · '+(r.bytes||0).toLocaleString()+' bytes'));const ul=el('ul','report-list');files.forEach(f=>{const li=el('li');li.append(el('b','',f.name+' · '+f.size.toLocaleString()+' bytes'));if(f.note)li.append(el('p','',f.note));if(f.binary){ul.append(li);return;}const details=el('details'),summary=el('summary','','View file contents');details.id='report-file-'+encodeURIComponent(f.name);summary.id='report-summary-'+encodeURIComponent(f.name);restoreDisclosure(details);details.append(summary,el('pre','',f.text||''));li.append(details);ul.append(li);});root.append(ul);}$('export-report').hidden=!files.length;$('prepare-report').textContent=files.length?'Refresh preview':'Preview files';showDialog(d);
}
$('undo').onclick=()=>send('undo');$('undo').title='Undo the last change (Ctrl+Z)';
// Ctrl+Z undoes the last change while the bridge offers it (not inside a text field).
if(document.addEventListener)document.addEventListener('keydown',e=>{if((e.ctrlKey||e.metaKey)&&!e.shiftKey&&!e.altKey&&String(e.key).toLowerCase()==='z'&&state?.notice?.undo&&!/^(text|textarea)$/i.test(e.target?.type||'')&&!$('report-dialog').open){e.preventDefault();send('undo');}});
$('dismiss').onclick=()=>send('dismiss');$('play').onclick=()=>send('play');
$('close-report').onclick=()=>{$('report-dialog').close();send('closeReport');};$('report-dialog').addEventListener('cancel',()=>send('closeReport'));$('prepare-report').onclick=()=>{if(!sample){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report…';$('cancel-report').hidden=false;$('prepare-report').disabled=true;}send('prepareReport',{issue:$('issue').value,logs:$('include-logs').checked,settings:$('include-settings').checked,system:$('include-system').checked,save:!!$('include-save').checked});};$('cancel-report').onclick=()=>send('cancelReport');$('export-report').onclick=()=>send('exportReport');
window.chrome?.webview?.addEventListener('message',e=>{const data=typeof e.data==='string'?JSON.parse(e.data):e.data;if(data.type==='state'){render(data);if(sample)reviewView(true);}else if(data.type==='progress'){$('report-progress').hidden=false;$('report-progress').textContent='Preparing report: '+data.done+' / '+data.total;$('cancel-report').hidden=false;}});
// Review views (--screenshot-view) adjust the sample, or the host's review state, before rendering again.
function reviewView(host) {
 {if(sampleView==='error')state.writeError={text:'Settings not saved. Nothing was changed.\nThe file is in use by another application (Windows error 32). Close it and try again.',retry:true};if(sampleView==='notice'){state.settings.find(s=>s.id==='exp_hair_groom_primary').value=0;}if(sampleView==='notice')state.notice={text:'RTX Hair: On \u2192 Off',also:['Strand hair edge resolve: Analytic coverage \u2192 Off'],anchor:'exp_hair_groom_primary',undo:true};if(sampleView==='missing'&&!host){state.gamePath='';state.canPlay=false;}if(sampleView==='probe'&&!host){state.compatibility={gpu:'Graphics unknown',vram:'Unknown',driver:'Unknown',warnings:['Graphics compatibility could not be checked. Recheck graphics or choose an adapter in Help & support.'],adapters:[{value:0,label:'Discrete graphics adapter'},{value:1,label:'Integrated graphics adapter'}],selected:-1};}render(state);}
 if(sampleView==='restart'){state.restartNeeded=true;state.restartCount=2;state.restartNames=['Hair shadows','Water lighting'];state.restartBanner='Restart the game to apply: Hair shadows, Water lighting.';state.notice={text:'Water lighting: Off \u2192 Reflections (after a game restart)',also:[],anchor:'exp_water',undo:true};render(state);}
 if((query.has('report')||sampleView==='report')&&!state.report?.files?.length){state.report={visible:true,privacy:'Creates a ZIP on your PC with BG3RTX logs, graphics settings, version and system graphics details. Logs may contain file paths. We remove common personal path details, but you should review the files before sharing. Nothing is uploaded. Screenshots and crash dumps are excluded; your latest save is added only if you tick it.',files:[{name:'CONTENTS.txt',size:248,text:'Sample preview only. No files have been collected.'},{name:'graphics.txt',size:112,text:'Sample adapter: NVIDIA GeForce RTX 5090\nDriver: 621.01\nAdapter used by the game: unconfirmed'}],bytes:360};renderReport();}
 if(sampleView==='experimental'||query.has('bottom'))$('main').scrollTop=$('main').scrollHeight;
}
if(sample) {
 const unqualified=['scene_handout','dlss5_nr'];
 const restartIds=['rt_geometry','exp_hidden_surface','exp_water','frame_generation','detailed_logging','dlss5_nr','scene_handout','water_surface_fresnel','fg_present_metering','exp_native_coverage','hair_motion_stability'];
 const choices=(id,labels,current)=>labels.map((label,value)=>({label,value,enabled:true,maturity:value&&unqualified.includes(id)?'awaiting_qualification':'qualified',apply:restartIds.includes(id)&&value!==current&&!(labels[0]==='Off'&&value===0&&id!=='rt_geometry'&&id!=='frame_generation'&&id!=='exp_hidden_surface'&&id!=='detailed_logging'&&id!=='dlss5_nr')?'Restart the game to apply':'Applies at once'}));
 const sampleMeta={"rr":{"cost":"Not measured","vram":"","provenance":"","details":"Needs an NVIDIA RTX GPU. Select DLSS as the upscaler in the game's graphics options.","presets":"Off · On · On · On"},"rt_geometry":{"cost":"Not measured","vram":"","provenance":"","details":"Off turns off all ray-traced lighting, shadows, reflections and water.","presets":"Off · Solid surfaces · Include foliage · Include foliage"},"sun_shadows":{"cost":"Measured: Include hidden roofs: about 0.3–1.3 ms more","vram":"","provenance":"RTX 5090, depending on the scene.","details":"Include hidden roofs also keeps shadows from roofs and canopies the game hides near the camera.","presets":"Visible surfaces · Ray-traced · Ray-traced · Include hidden roofs"},"gi":{"cost":"Not measured","vram":"","provenance":"","details":"Ray-traced high traces two rays per pixel for less noise.","presets":"Visible surfaces · Ray-traced · Ray-traced · Ray-traced high"},"local_shadows":{"cost":"Not measured","vram":"","provenance":"","details":"Fast needs DLSS Ray Reconstruction. Full keeps the game's own shadows where they are needed.","presets":"Off · Fast · Full · Full"},"reflections":{"cost":"Not measured","vram":"","provenance":"","details":"Needs Ray-tracing detail and Indirect lighting. Without DLSS Ray Reconstruction, only mirror-like reflections are traced.","presets":"Off · Glossy · Standard · Rough"},"frame_generation":{"cost":"Not measured","vram":"","provenance":"","details":"Needs NVIDIA Reflex On or On + Boost. Turning it on or off needs a game restart; switching between 2x, 3x and 4x applies while you play. Generated frames make motion smoother but do not raise the rendered frame rate.","presets":"2x in every preset"},"rt_intensity":{"cost":"Not measured","vram":"","provenance":"","details":"","presets":"100% in every preset"},"fps_cap":{"cost":"Not measured","vram":"","provenance":"","details":"Ultra limits rendering to 58 fps. A limit is a ceiling, not a target.","presets":"Off · Off · Off · 58 fps"},"reflex":{"cost":"Not measured","vram":"","provenance":"","details":"Frame Generation needs On or On + Boost.","presets":"On in every preset"},"exp_water":{"cost":"Estimate: 0.5–0.7 ms","vram":"Up to 520 MB","provenance":"Estimated water cost: 0.5–0.7 ms and up to 520 MB on RTX 5090 at Water Queens House.","details":"Needs Ray-tracing detail. Turning water on or raising its level needs a game restart; lowering it or turning it off applies while you play. Uses up to about 520 MB of video memory.","presets":"Off · Off · Off · Include refraction"},"dlss5_nr":{"cost":"Not measured","vram":"","provenance":"","details":"Experimental and off in every preset. Requires Ray Reconstruction before post effects and a compatible user-provided nvngx_dlssnr.dll. The launcher prepares the runtime before Play; custom or unsigned runtimes are identified in the log and may be rejected by NVIDIA (0xbad0000b). Restart after changing this option or the runtime file.","presets":"Off in every preset"},"water_surface_fresnel":{"cost":"Not measured","vram":"","provenance":"","details":"Needs Water lighting. Turning it on needs a game restart; turning it off applies while you play. Compare the reflections on shallow pools.","presets":"Off in every preset"},"fg_destroy_wait_exact":{"cost":"Not measured","vram":"","provenance":"","details":"Compare smoothness while turning in the same scene. Experimental; disabled in every preset.","presets":"Standard in every preset"},"fg_present_metering":{"cost":"Not measured","vram":"","provenance":"","details":"Requires Frame generation and a game restart. Compare 3x smoothness in the same scene. Disabled in every preset.","presets":"Standard in every preset"},"exp_hidden_surface":{"cost":"Estimate: Near about 0.6 ms; Far about 4 ms","vram":"","provenance":"Estimated cost on RTX 5090: about 0.6 ms for Near or 4 ms for Far.","details":"Far carries sunlight bounce up to 6 m. Needs Ray-tracing detail: Include foliage and Indirect lighting: Ray-traced. Turning it on or off needs a game restart; switching between Near and Far may briefly pause the game.","presets":"Off · Off · Off · Far (6 m)"},"exp_gi_rt_pipeline":{"cost":"Measured: Saves about 2 ms","vram":"Under 1 MB","provenance":"RTX 5090, Ultra, live 2026-11-08: GI trace -2.0 ms at the inn (12.0 to 9.95 ms) and -2.1 ms at the camp; scene energy unchanged within noise.","details":"Traces the off-screen bounce light with the GPU's ray tracing pipeline instead of a compute pass. Applies to Off-screen bounce light: without it this option does nothing. The first time, the pipeline builds for up to a minute while the usual trace keeps running. Without the GPU's ray tracing pipeline and reordering features the usual trace runs.","presets":"On in every preset"},"exp_gi_rt_pipeline_reorder":{"cost":"Measured: Saves about 0.8 to 1.5 ms","vram":"","provenance":"RTX 5090, Ultra, live 2026-11-08 on top of the ray tracing pipeline trace: camp -1.5 / -1.6 ms (two runs), inn -0.8 ms (one clean window); scene energy unchanged. Shipped on the user's decision; the inn result is one window.","details":"Sorts the bounce rays by what they hit before shading them (shader execution reordering). Needs Faster bounce light tracing. By hit kind is the default; by hit kind and object is the alternative. Without the GPU's reordering support the rays are traced unsorted.","presets":"By hit kind in every preset"},"scene_handout":{"cost":"Measured: None (CPU wait only)","vram":"Under 1 MB","provenance":"RTX 5090, Ultra, Water Queens House, live 2026-11-09 (before the RTX Hair updates): wait per frame 4.4-5.1 ms to 0.5-0.7 ms, rendered frame time 24.6-25.2 ms to 24.2-24.6 ms (that scene is limited by the GPU), 0 fallbacks. No measurement yet at the inn, on High or with today's RTX Hair.","details":"Not qualified yet: off in every preset. Today every frame's ray-traced lighting waits 3 to 9 ms for that frame's scene to be built. On removes the wait: the lighting is recorded at once and reads the scene when the GPU runs it, so the image is the same. How much frame rate this gives depends on the scene: little where the GPU is the limit. If a frame's scene is not ready in time, that frame uses the previous frame's scene (as it does today after a long wait): moving characters' shadows can lag by one frame in such frames. It changes how the game's graphics device is set up, so turning it on needs a game restart and it may behave differently on other drivers; turning it off works while playing. After the first restart the lighting shaders rebuild once (up to a minute, the usual path runs meanwhile).","presets":"Off in every preset"},"box_lights":{"cost":"Measured: About 0.13 ms","vram":"","provenance":"RTX 5090 at Grymforge.","details":"Needs Local light shadows. Lights the game marks as not casting shadows stay unshadowed.","presets":"Off · Off · Off · On"},"rr_post_temporal":{"cost":"Measured: About 0.15 ms","vram":"250 MiB at 2560 × 1067","provenance":"250 MiB at 2560 × 1067 on RTX 5090 in dialogue.","details":"Blends background blur and bloom across frames. Needs DLSS Ray Reconstruction. The effect is subtle; it is most visible in dialogue close-ups, on hair and fine edges against a blurred background.","presets":"Off · Off · Off · On"},"exp_native_coverage":{"cost":"Measured: Ray tracing 2.3–6.3 ms; indirect light 3.2–8.8 ms","vram":"","provenance":"Recorded RTX 5090 increases: 2.3–6.3 ms in the ray-tracing pass and 3.2–8.8 ms in indirect-light tracing, depending on scene.","details":"Needs Ray-tracing detail: Include foliage. Turning it on needs a game restart, and the first start may pause while shaders compile. Visible hair stays the game's own hair.","presets":"Off · Off · Off · On"},"exp_hair_groom_primary":{"cost":"Not measured","vram":"","provenance":"","details":"Beta: supported, off in every preset. A style gets strands after the game has shown it once and the launcher has converted it; until then it keeps the game's look. Turns on and off while playing. Requires linear swept spheres, shaderInt64 and fragment stores. Hair styles get strands after the game has shown them once and the launcher has converted them. Turns on all of RTX Hair: strands on screen and in ray-traced lighting (frame record decided at submit, per-frame trace tags, LOD keep, coverage hold, world-space tangent), strand shadow and AO transmittance, no card shadows, physical fibre shading (also indirect, specular and shadow-mapped lights), RR hair guides and physical hair ray origins. Cost remains unqualified. It turns on and off while playing, also with its keyboard shortcut (Ctrl+Shift+F7 by default); if the game started with RTX Hair off, the physical hair ray origins need a game restart.","presets":"Off in every preset"},"hair_distant_detail":{"cost":"Measured: Saves about 0.08 ms (1/4) or 0.10 ms (1/8)","vram":"About 75-100 MB","provenance":"RTX 5090, one scene with 13 characters at a distance (2026-11-10). Not yet qualified.","details":"Only changes RTX Hair. Full traces every strand at every distance. 1/4 (Standard): small, distant heads trace down to a quarter of their strands, each drawn wider so the hair covers the same area; the outline stays as it is today. 1/8 (Sharper): those heads trace an eighth; finer-looking and slightly cheaper, but the hair outline is about 2% fuller than physically correct. Switch while watching a character a few metres away.","presets":"Full in every preset"},"hair_motion_stability":{"cost":"Not measured","vram":"","provenance":"","details":"Only changes RTX Hair. Strand pixels take the motion of the hair they belong to, so the denoiser stops dragging old hair behind moving heads. Turning it on takes effect after a game restart (the hair motion shaders are prepared when the game starts); turning it off works while playing. Watch a character walk or turn the camera.","presets":"Off · Off · On · On"},"exp_hair_strand_aa":{"cost":"Not measured","vram":"","provenance":"","details":"How on-screen strand edges enter Ray Reconstruction. The game's hair dither resolve no longer mixes strands by the hair cards' dither pattern; with Analytic coverage, thin strands at the edges are blended with their background instead of flickering. Appearance and cost are unqualified. 5n: compare strand edges against the background in Test Dialogue1 (hair silhouette, flyaway strands): dithering at the edges should be gone with Analytic coverage.","presets":"Off in every preset"},"detailed_logging":{"cost":"Not measured","vram":"","provenance":"","details":"","presets":"Off in every preset"}};
 const settings=[
 ['rr','DLSS Ray Reconstruction','Lighting',['Off','On'],1,'Reduces noise in ray-traced lighting.'],
 ['rt_geometry','Ray-tracing detail','Lighting',['Off','Solid surfaces','Include foliage'],2,'Chooses what ray-traced lighting and shadows include: solid surfaces only, or also foliage, grates and chains.'],
 ['sun_shadows','Sun shadows','Lighting',['Visible surfaces','Ray-traced','Include hidden roofs'],2,'Ray-traced sun shadows. The top level keeps the shadows of roofs the game hides.'],
 ['gi','Indirect lighting','Lighting',['Off','Visible surfaces','Ray-traced','Ray-traced high'],3,'Bounced light and soft shading where surfaces meet.'],
 ['local_shadows','Local light shadows','Lighting',['Off','Fast','Full'],2,'Adds shadows from torches, lamps and spells.'],
 ['rt_intensity','Mod lighting strength','Lighting',[],100,"Blends indirect lighting, local shadows and reflections toward the game's original appearance at lower values."],
 ['exp_hidden_surface','Off-screen bounce light','Lighting',['Off','Near','Far (6 m)'],2,"Bounced light and glow from surfaces outside the camera's view."],
 ['box_lights','Interior light shadows','Lighting',['Off','On'],1,'Shadows from rectangular interior lights that are meant to cast them.'],
 ['rr_post_temporal','Depth of field and bloom smoothing','Surfaces',['Off','On'],1,'Steadier background blur and bloom around fine edges.'],
 ['exp_native_coverage','Hair shadows','Lighting',['Off','On'],1,"The game's own hair casts shadows and contact shading on faces and necks."],
 ['exp_hair_groom_primary','RTX Hair','Lighting',['Off','On'],1,'Hair as individual ray-traced strands instead of flat cards.'],
 ['hair_distant_detail','RTX Hair detail at distance','Lighting',['Full','1/4 (Standard)','1/8 (Sharper)'],0,'Fewer, wider strands on distant heads. Same look, lower cost.'],
 ['hair_motion_stability','RTX Hair motion stability','Lighting',['Off','On'],0,'Less smearing and fewer trails behind moving RTX Hair.'],
 ['exp_gi_rt_pipeline','Faster bounce light tracing','Performance',['Off','On'],1,'The same bounce lighting, traced faster.'],
 ['exp_gi_rt_pipeline_reorder','Bounce trace reordering','Performance',['Off','By hit kind','By hit kind and object'],1,'The same bounce lighting, traced faster again.'],
 ['scene_handout','Low-latency scene','Performance',['Off','On'],0,"Experimental: lighting stops waiting for each frame's scene build. Same image, less CPU wait."],
 ['reflections','Reflections','Surfaces',['Off','Glossy','Standard','Rough'],3,'Adds reflections to smooth surfaces and, at higher levels, rougher surfaces.'],
 ['exp_water','Water lighting','Surfaces',['Off','Reflections','Reflections and shadows','Include refraction'],3,'Water reflections, shadows and, at the highest level, a refracted view of the riverbed or pool floor.'],
 ['frame_generation','Frame Generation','Performance',['Off','2x','3x','4x'],1,'Adds generated frames between rendered frames for smoother motion.'],
 ['fps_cap','Rendered frame limit','Performance',[],58,'Limits the frame rate before Frame Generation adds frames.'],
 ['reflex','NVIDIA Reflex','Performance',['Off','On','On + Boost'],1,'Reduces the delay between your input and the image on screen.'],
 ['detailed_logging','Detailed logging','Support',['Off','On'],0,'Writes extra performance and diagnostic details to the log for bug reports. Leave it off for normal play.'],
 ['dlss5_nr','DLSS 5 Neural Rendering','DLSS 5',['Off','On'],0,'Neural relighting after Ray Reconstruction, before post effects. Needs your own DLSS 5 runtime file.'],
 ['water_surface_fresnel','Water surface reflections','Experimental',['Off','On'],0,"Experimental: works out the reflection at the water surface separately from the water's depth colour. Mostly visible at the edges of shallow pools; open water barely changes."],
 ['fg_destroy_wait_exact','Frame generation pacing test','Experimental',['Standard','Deferred'],0,'Experimental: may reduce frame generation stutter when the game replaces temporary images. Cost and smoothness are unqualified.'],
 ['fg_present_metering','Multi-frame presentation test','Experimental',['Standard','Driver metering'],0,'Experimental: enables an optional driver presentation capability for frame generation. Smoothness and cost are unqualified.'],
 ['exp_hair_strand_aa','Strand hair edge resolve','Experimental',['Off','Strands unresolved','Analytic coverage'],0,'Experimental: blends thin strand edges by their exact pixel coverage instead of dithering.']
 ].filter(x=>x[2]!=='Experimental'||experimentalOpen).map(([id,title,category,levels,value,description])=>({id,title,category,value,description,choices:choices(id,levels,value),...sampleMeta[id],apply:['rt_geometry','detailed_logging','dlss5_nr','fg_present_metering'].includes(id)?'Restart the game to apply':['rr','exp_gi_rt_pipeline_reorder'].includes(id)?'Applies at once; may briefly pause':'Applies at once',...(id==='rt_intensity'?{min:50,max:100,step:5,unit:'%'}:id==='fps_cap'?{min:30,max:240,step:1,unit:'fps',hasOff:true}:{})}));
 // RTX Hair conversion: ?view=granny has the player's granny2.dll and All styles; ?view=hairerror a stopped run.
 const granny=sampleView==='granny',stopped=sampleView==='hairerror';
 const preparation={items:[
  {id:'hair',title:'RTX Hair',line:granny?'412 of 1530 styles ready, 1.1 GB on disk.':'412 of 559 styles ready, 1.1 GB on disk.',detail:stopped?'Conversion stopped: granny2.dll could not be loaded.':granny?'Converting all styles, about 2 h left.':'Converting styles seen in game, about 35 min left.',progress:granny?0.27:0.74,state:stopped?'failed':'running'},
  {id:'shaders',title:'Shaders',line:'Preparing shaders: 41 of 71.',detail:'Your graphics driver still builds the first ray tracing pipeline in game, about a minute.',progress:41/71,state:'running'}],
  convertAll:granny,whilePlaying:false,paused:false,pausable:true,
  hair:{ready:412,captured:430,...(granny?{total:1530,source:'all',etaSeconds:7300}:{source:'seen',etaSeconds:2100}),finished:granny?412:430,needed:granny?1530:559,bytes:1.1e9,...(stopped?{error:'granny2.dll could not be loaded.'}:{})},
  granny:granny||stopped?{path:'C:\\Tools\\ExportTool\\granny2.dll',ok:true}:{path:'',ok:false},convertAllReason:granny||stopped?'':'Needs granny2.dll: choose it below.'};
 // The WebView host sends its review state (real settings table); a plain browser uses this sample.
 if(!window.chrome?.webview){render({type:'state',version:'Sample',modStatus:'Mod files present',preset:3,presetLabel:'Ultra',recommendedPreset:3,custom:true,presetDescriptions:['Lightest. Lighting and sun shadows use visible surfaces only; ray tracing and DLSS Ray Reconstruction are off.','Ray-traced lighting and sun shadows on solid surfaces, Fast local light shadows and Glossy reflections.','Adds foliage to ray tracing, Full local light shadows and Standard reflections.','Adds hidden-roof, interior light and hair shadows, two-ray indirect lighting, Rough reflections, water lighting, Far off-screen bounce light and depth of field smoothing. Limits rendering to 58 fps.'],experimental:experimentalOpen,frameGeneration:'2x',frameGenerationNote:'Configured; displayed frames include generated frames',canPlay:true,gamePath:'C:\\Games\\Baldurs Gate 3',compatibility:{gpu:'NVIDIA GeForce RTX 5090',vram:'32 GB',driver:'Driver 621.01',warnings:[],details:['Hardware ray tracing supported.','Driver 621.01 supported.']},settings,status:'',
  shortcuts:{hotkey_rt_toggle:'Ctrl+Shift+F9',hotkey_dlss5:'Ctrl+Shift+F8',hotkey_rtx_hair:'Ctrl+Shift+F7'},shortcutDefaults:{hotkey_rt_toggle:'Ctrl+Shift+F9',hotkey_dlss5:'Ctrl+Shift+F8',hotkey_rtx_hair:'Ctrl+Shift+F7'},preparation});
 reviewView(false);}
} else send('init');
