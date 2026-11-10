// Execute the shipped renderer with a minimal DOM. No browser, network or game.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const fixture=JSON.parse(fs.readFileSync(0,'utf8'));
const ids=new Map();
class Element {
 constructor(tag){this.tagName=tag;this.children=[];this.style={setProperty(){}};this.attributes={};this.open=false;this.textContent='';}
 set id(v){this._id=v;ids.set(v,this);} get id(){return this._id;}
 append(...items){this.children.push(...items);} replaceChildren(...items){this.children=[];this.append(...items);}
 setAttribute(k,v){this.attributes[k]=v;} addEventListener(){} showModal(){this.open=true;} close(){this.open=false;}
 focus(){document.activeElement=this;}
 blur(){document.activeElement=null;if(this.onblur)this.onblur();}
 contains(target){return this===target||this.children.some(c=>typeof c==='object'&&c.contains(target));}
 querySelectorAll(selector){const tags=selector.split(',');return this.children.flatMap(c=>typeof c==='object'?[...(tags.includes(c.tagName)?[c]:[]),...c.querySelectorAll(selector)]:[]);}
}
const document={head:new Element('head'),documentElement:new Element('html'),activeElement:null,
 createElement:t=>new Element(t),createTextNode:t=>t,getElementById:id=>ids.get(id),querySelector:()=>new Element('img')};
for(const match of fs.readFileSync('tools/launcher/ui/index.html','utf8').matchAll(/id="([^"]+)"/g)){const e=new Element('div');e.id=match[1];}
ids.get('report-dialog').append(ids.get('report-preview'));
let receive;const sent=[];
const context=vm.createContext({document,location:{search:'',protocol:'file:'},URLSearchParams,window:{chrome:{webview:{postMessage:m=>sent.push(m),addEventListener:(_,fn)=>receive=fn}}}});
vm.runInContext(fs.readFileSync('tools/launcher/ui/app.js','utf8'),context);
const render=s=>receive({data:{...s,type:'state'}});
const all=e=>[e,...e.children.flatMap(c=>typeof c==='object'?all(c):[])];
render(fixture);
assert(!ids.get('navigation').children.some(e=>e.id==='nav-Developer'));
// Home: first in the navigation and the page the launcher opens on. A dashboard: Play (preset, Play, version and GPU),
// RTX Hair (its Lighting control, hotkey and conversion), Status and Shaders. No performance shortcuts.
{
 const msg=()=>JSON.parse(JSON.stringify(sent.at(-1)));
 const home=()=>all(ids.get('section-content'));
 assert.strictEqual(ids.get('navigation').children[0].id,'nav-Home','Home is first');
 assert.strictEqual(ids.get('nav-Home').attributes['aria-current'],'page','Home is the default page');
 assert(ids.get('header').hidden,'Home carries the preset and GPU line itself');
 assert(ids.get('play').hidden,'one Play button on Home');
 for(const id of ['home-play-card','home-hair','home-attention','home-shaders'])assert(home().includes(ids.get(id)),id+' missing on Home');
 assert(!ids.get('home-performance')||!home().includes(ids.get('home-performance')),'no performance shortcuts on Home');
 assert(home().includes(ids.get('preset-3'))&&home().includes(ids.get('home-play')));
 assert(ids.get('home-status').textContent.includes(fixture.version),'version on the Play card');
 for(let p=0;p<4;p++){ids.get('preset-'+p).onclick();assert.deepStrictEqual(msg(),{type:'preset',value:p});}
 assert.strictEqual(ids.get('home-play').disabled,!fixture.canPlay,'Home Play follows canPlay like the footer');
 ids.get('home-play').onclick();assert.deepStrictEqual(msg(),{type:'play'});
 // The report flow is reachable from Home (Play card), the same 'report' message as Support.
 assert(all(ids.get('home-play-card')).includes(ids.get('home-report-open')),'report link on the Home Play card');
 assert(/diagnostic report/i.test(ids.get('home-report-open').textContent));
 ids.get('home-report-open').onclick();assert.deepStrictEqual(msg(),{type:'report'});
 // RTX Hair: the same control and message as its Lighting card.
 const act=id=>{const e=ids.get(id);assert(e,id+' missing');if(e.type==='checkbox'){e.checked=!e.checked;e.onchange();}else e.onclick();return msg();};
 {
  render(fixture);const id='exp_hair_groom_primary',control='control-'+id+'-0';
  assert(all(ids.get('home-hair')).includes(ids.get(control)),control+' not on Home');
  assert(ids.get(control).attributes['aria-describedby']==='needs-'+id);
  const fromHome=act(control);assert.strictEqual(fromHome.type,'set');assert.strictEqual(fromHome.id,id);
  ids.get('nav-Lighting').onclick();assert(!ids.get('header').hidden&&!ids.get('play').hidden);
  assert(all(ids.get('card-'+id)).includes(ids.get(control)),control+' not on Lighting');
  assert.deepStrictEqual(act(control),fromHome,id+' sends the same message from Home and Lighting');
  ids.get('nav-Home').onclick();
 }
 const hairSetting=fixture.settings.find(s=>s.id==='exp_hair_groom_primary');
 const badge=ids.get('home-hair').children[0].children[1].textContent;
 assert.strictEqual(badge,/restart/i.test(hairSetting.apply)?'Restart to apply':'Applies after saving','RTX Hair keeps its apply badge');
 // 5m: RTX Hair is Apply::Live, so the bridge sends the live label and Home shows it; the hotkey comes from the bridge.
 assert.strictEqual(hairSetting.apply,'Applies after saving','RTX Hair applies while playing');
 assert.strictEqual(badge,'Applies after saving');
 assert.strictEqual(fixture.shortcuts.hotkey_rtx_hair,'Ctrl+Shift+F7','the bridge sends the RTX Hair hotkey');
 // Status: game folder, GPU, mod files, unsaved changes (with Save), restart when needed.
 for(const id of ['game','gpu','mods','unsaved'])assert(ids.get('home-status-'+id),'status '+id);
 assert(all(ids.get('home-play-card')).includes(ids.get('home-attention')),'status is part of the Play card (first at every width)');
 // In-game keys at a glance, changed on Hotkeys.
 for(const [id,v] of Object.entries(fixture.shortcuts))assert(all(ids.get('home-keys')).some(e=>e.tagName==='kbd'&&e.textContent===v),id+' on Home');
 ids.get('home-keys-change').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');ids.get('nav-Home').onclick();
 render({...fixture,unsavedCount:2,dirty:2,restartNeeded:true,restartBanner:'Restart the game to apply 2 changes',gamePath:''});
 assert(/2 unsaved changes/.test(ids.get('home-status-unsaved').children[1].textContent));
 ids.get('home-save').onclick();assert.deepStrictEqual(msg(),{type:'save',policy:'enable'});
 assert(ids.get('home-status-restart').children[1].textContent.startsWith('Restart the game to apply 2 changes'));
 ids.get('home-browse').onclick();assert.deepStrictEqual(msg(),{type:'browseGame'});
 // Preparation: RTX Hair numbers, progress with ETA, Start/Pause, source with granny2.dll, When to convert; Shaders.
 assert(!ids.get('prep-pause')||!home().includes(ids.get('prep-pause')),'no hair actions without preparation data');
 const prep={items:[{id:'hair',title:'RTX Hair',line:'3 of 5 styles ready, 0.1 GB on disk.',detail:'Converting styles seen in game, about 2 min left.',progress:0.6,state:'running'},
  {id:'shaders',title:'Shaders',line:'Preparing shaders: 4 of 9.',detail:'Your graphics driver still builds the first ray tracing pipeline in game, about a minute.',progress:0.4,state:'running'}],
  whilePlaying:false,paused:false,pausable:true,convertAll:false,hair:{ready:3,captured:4,needed:5,finished:3,bytes:5e7,etaSeconds:120,source:'seen'},
  granny:{path:'',ok:false},convertAllReason:'Needs granny2.dll: choose it below.'};
 render({...fixture,preparation:prep});
 assert(all(ids.get('home-hair')).includes(ids.get('prep-hair'))&&all(ids.get('home-shaders')).includes(ids.get('prep-shaders')));
 assert.strictEqual(ids.get('hair-stat-ready').children[0].textContent,'3');assert.strictEqual(ids.get('hair-stat-captured').children[0].textContent,'4');
 assert.strictEqual(ids.get('hair-stat-total').children[0].textContent,'–','unknown total shown as a dash');
 assert.strictEqual(ids.get('hair-stat-disk').children[0].textContent,'50 MB');
 assert(all(ids.get('prep-hair')).some(e=>e.textContent==='60% · about 2 min left'),'progress with ETA');
 assert(all(ids.get('home-shaders')).some(e=>e.textContent==='Preparing shaders: 4 of 9.')&&all(ids.get('home-shaders')).some(e=>/first ray tracing pipeline/.test(e.textContent)));
 assert(ids.get('prep-progress-shaders'),'shader progress bar');
 ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'pause',value:0});
 ids.get('prep-when-1').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'whilePlaying',value:1});
 ids.get('prep-when-0').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'whilePlaying',value:0});
 // Convert all styles: disabled with its one-line reason until granny2.dll is set; the picker is always there.
 assert(ids.get('prep-source-1').disabled&&ids.get('prep-source-1').title===prep.convertAllReason);
 assert(!ids.get('prep-source-reason').hidden&&ids.get('prep-source-reason').children[1].textContent===prep.convertAllReason);
 assert.strictEqual(ids.get('prep-source-0').attributes['aria-pressed'],'true');
 ids.get('prep-granny-browse').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'browseGranny',value:0});
 assert.strictEqual(ids.get('prep-granny-help').textContent,"Convert all styles needs granny2.dll, which BG3RTX cannot include. If you have LSLib's ExportTool (github.com/Norbyte/lslib, Releases), point to the granny2.dll in its folder.");
 // The host's line wins; --all progress per tier is listed in conversion order.
 render({...fixture,preparation:{...prep,grannyHelp:'Host line.',hair:{...prep.hair,source:'all',total:1217,tiers:[{tier:'companion',total:6,finished:6},{tier:'player',total:432,finished:400}]}}});
 assert.strictEqual(ids.get('prep-granny-help').textContent,'Host line.');
 assert.strictEqual(ids.get('prep-hair-tiers').textContent,'Companions 6/6 · Character creation 400/432');
 render({...fixture,preparation:{...prep,granny:{path:'C:\\Tools\\granny2.dll',ok:true},convertAllReason:''}});
 assert(!ids.get('prep-source-1').disabled&&ids.get('prep-source-reason').hidden);
 ids.get('prep-source-1').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'convertAll',value:1});
 ids.get('prep-granny-clear').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'clearGranny',value:0});
 render({...fixture,preparation:{...prep,paused:true}});ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'resume',value:0});
 // A stopped run: Try again sends start; the last error is shown.
 render({...fixture,preparation:{...prep,items:[{...prep.items[0],state:'failed'},prep.items[1]],hair:{...prep.hair,error:'granny2.dll could not be loaded.'}}});
 assert.strictEqual(ids.get('prep-pause').textContent,'Try again');ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'start',value:0});
 assert.strictEqual(ids.get('prep-hair-error').textContent,'Last error: granny2.dll could not be loaded.');
 // Tolerant of an older host: no hair facts, no granny, no reason.
 render({...fixture,preparation:{items:prep.items,whilePlaying:false,paused:false,pausable:true}});
 assert(all(ids.get('prep-hair')).some(e=>e.textContent==='3 of 5 styles ready, 0.1 GB on disk.'),'host line without numbers');
 assert(!ids.get('prep-source-1').disabled,'no reason, no block');
 // Unavailable converter: the line, no actions.
 render({...fixture,preparation:{...prep,items:[{id:'hair',title:'RTX Hair',line:'Not available: the hair converter is not installed.',state:'unavailable'}]}});
 assert(!all(ids.get('home-hair')).some(e=>e.id&&/^prep-(pause|source|when|granny)/.test(e.id)),'no actions without a converter');
 // The RTX Hair hotkey (when the bridge has the binding) links to Hotkeys.
 render({...fixture,shortcuts:{...fixture.shortcuts,hotkey_rtx_hair:'Ctrl+Shift+H'}});
 const hk=ids.get('home-hotkey-exp_hair_groom_primary');assert(hk&&all(hk).some(e=>e.tagName==='kbd'&&e.textContent==='Ctrl+Shift+H'));
 ids.get('home-hotkey-change-exp_hair_groom_primary').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');
 assert(ids.get('hotkey_rtx_hair'),'RTX Hair hotkey on the Hotkeys page');
 render(fixture);ids.get('nav-Home').onclick();
 assert(!ids.get('home-hotkey-exp_hair_groom_primary')||!home().includes(ids.get('home-hotkey-exp_hair_groom_primary'))||fixture.shortcuts?.hotkey_rtx_hair!==undefined,'no hotkey line without a binding');
}
for(const category of new Set(fixture.settings.map(s=>s.category))){
 assert(ids.has('nav-'+category),category+' has no page');ids.get('nav-'+category).onclick();
 const cards=all(ids.get('section-content')).filter(e=>e.tagName==='article'&&e.id);
 for(const s of fixture.settings.filter(s=>s.category===category)){
  assert(cards.some(c=>c.id==='card-'+s.id),s.id+' unreachable');
  const card=cards.find(c=>c.id==='card-'+s.id);
  assert(!all(card).some(e=>/GPU cost:|Not measured|VRAM: Unknown/.test(e.textContent)),s.id+' has unknown or duplicate cost');
  for(const control of card.querySelectorAll('button,input'))assert(control.id&&control.attributes['aria-describedby']);
  // Unmet requirements once per card; the full list lives in Details.
  const needs=ids.get('needs-'+s.id),reasons=needs.children.map(c=>c.textContent||'').join('');
  assert.strictEqual(needs.hidden,!s.choices.some(ch=>ch.reason),s.id+' needs visibility');
  for(const ch of s.choices){if(ch.reason)assert(reasons.includes(ch.reason.replace(/^Needs /,'')),s.id+' reason');}
  const detailText=all(card).filter(e=>e.className==='detail-copy').map(e=>e.textContent).join('\n');
  for(const ch of s.choices)if(ch.requirements)assert(detailText.includes(ch.requirements),s.id+' requirements in Details');
  for(const e of card.querySelectorAll('button,input'))assert(ids.has(e.attributes['aria-describedby']));
 }
}
ids.get('nav-DLSS 5').onclick();
assert(ids.get('dlss5-runtime-browse'));
assert(all(ids.get('card-dlss5_nr')).includes(ids.get('card-hotkey-dlss5_nr')),'the DLSS 5 card shows its hotkey');
ids.get('card-hotkey-change-dlss5_nr').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');
// Hotkeys: every binding the bridge sends, in one place.
for(const id of Object.keys(fixture.shortcuts))assert(all(ids.get('section-content')).includes(ids.get('hotkey-row-'+id)),id+' not on Hotkeys');
const nrKey=ids.get('hotkey_dlss5');
const event=key=>({key,ctrlKey:true,shiftKey:true,preventDefault(){},stopPropagation(){}});
let tabPrevented=false;
nrKey.onkeydown({...event('Tab'),preventDefault(){tabPrevented=true;}});
assert(!tabPrevented,'shortcut field blocks keyboard navigation');
let repeated=sent.length;
nrKey.onkeydown({...event('N'),repeat:true});
assert.strictEqual(sent.length,repeated,'held key changed shortcut');
nrKey.onkeydown({...event('!'),code:'Digit1'});
assert.strictEqual(sent.at(-1).value,'Ctrl+Shift+1','shifted digit cannot be captured');
let count=sent.length;nrKey.onkeydown(event('F9'));assert.strictEqual(sent.length,count,'conflict reached bridge');
nrKey.onkeydown(event('F10'));assert.strictEqual(sent.length,count,'reserved F10 reached bridge');
nrKey.onkeydown({...event('N'),ctrlKey:false,shiftKey:false,altKey:true});
assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'shortcut',id:'hotkey_dlss5',value:'Alt+N'});
assert(ids.get('hotkey_rt_toggle'));
const reset=ids.get('hotkey-reset-hotkey_rt_toggle');assert.strictEqual(reset.textContent,'Reset to '+fixture.shortcutDefaults.hotkey_rt_toggle);reset.onclick();
assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'resetShortcut',id:'hotkey_rt_toggle'});
ids.get('hotkey_rt_toggle').onkeydown(event('Delete'));assert.strictEqual(sent.at(-1).value,'None');
ids.get('nav-Lighting').onclick();
assert.deepStrictEqual(all(ids.get('section-content')).filter(e=>e.tagName==='article'&&e.id).map(e=>e.id),
 // RTX Hair (exp_hair_groom_primary) is a release Lighting card since 0.9.5, after Hair shadows.
 ['rt_geometry','rr','sun_shadows','gi','exp_hidden_surface','local_shadows','box_lights','exp_native_coverage','exp_hair_groom_primary','hair_distant_detail','hair_motion_stability','rt_intensity'].map(id=>'card-'+id));
assert(!ids.has('summary')&&!ids.has('report'),'duplicate summary or report entry');
const rr=ids.get('control-rr-0');assert(rr.type==='checkbox'&&rr.attributes['aria-label']);
rr.focus();render(fixture);assert.strictEqual(document.activeElement,ids.get(rr.id),'focus lost on bridge update');
const checkbox=ids.get(rr.id);let clicked=false;checkbox.click=()=>clicked=true;checkbox.onkeydown({key:'Enter',preventDefault(){}});assert(clicked);
for(let p=0;p<4;p++){ids.get('preset-'+p).onclick();assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'preset',value:p});}
assert(!ids.get('navigation').children.some(e=>e.id==='nav-Advanced'),'release has no Advanced controls, so no Advanced page');
assert(ids.get('navigation').children.some(e=>e.id==='nav-Hotkeys'));
assert(!ids.get('navigation').children.some(e=>e.id==='nav-Memory'));
assert(!all(ids.get('section-content')).some(e=>e.id==='experimental-summary'));
ids.get('nav-Support').onclick();
assert(all(ids.get('section-content')).some(e=>e.textContent.includes("In game, Ctrl+Shift+F9 switches all ray-traced lighting off and on")&&e.textContent.includes('Change it on Hotkeys')));
assert(all(ids.get('section-content')).some(e=>e.textContent==='Video memory'));
assert(all(ids.get('section-content')).some(e=>e.textContent==='Compatibility'));
assert(ids.get('support-report')&&ids.get('support-browse')&&ids.get('support-recheck'));
assert(ids.get('card-detailed_logging')&&ids.get('control-detailed_logging-0').type==='checkbox');
const notices=all(ids.get('section-content')).find(e=>e.tagName==='a');
assert(notices&&notices.textContent==='Fonts and third-party notices');
notices.onclick({preventDefault(){}});assert.strictEqual(sent.at(-1).type,'notices');
// Explicit bridge authorization, rather than the presence of a developer row.
const dev={...fixture.settings[0],id:'test-dev',developer:true};
render({...fixture,settings:[...fixture.settings,dev]});assert(!ids.get('navigation').children.some(e=>e.id==='nav-Developer'));
render({...fixture,developer:true,settings:[...fixture.settings,dev]});ids.get('nav-Advanced').onclick();assert.strictEqual(ids.get('experimental-summary').tagName,'summary');ids.get('nav-Developer').onclick();assert(all(ids.get('section-content')).some(e=>e.id==='card-test-dev'));
// Card disclosures and focus survive periodic bridge refreshes.
render(fixture);ids.get('nav-Lighting').onclick();
const details=all(ids.get('section-content')).find(e=>e.tagName==='details'&&e.id?.startsWith('details-'));
assert(details,'setting Details fixture missing');details.open=true;
const detailSummary=details.children.find(e=>e.tagName==='summary');detailSummary.focus();render(fixture);
assert(ids.get(details.id).open,'setting disclosure collapsed');assert.strictEqual(document.activeElement,ids.get(detailSummary.id));
// Explicit Off has a pressed state; the inactive range stays inside its numeric bounds.
const slider={id:'test-cap',title:'Frame cap',category:'Performance',value:0,min:30,max:240,step:1,unit:'fps',hasOff:true,choices:[],apply:'Live'};
render({...fixture,settings:[...fixture.settings,slider]});ids.get('nav-Performance').onclick();
let cap=ids.get('card-test-cap'),inputs=cap.querySelectorAll('input'),buttons=cap.querySelectorAll('button');
assert.strictEqual(buttons[0].attributes['aria-pressed'],'true');assert.strictEqual(buttons[1].attributes['aria-pressed'],'false');
assert(inputs[0].disabled);assert.strictEqual(inputs[0].value,30);assert.strictEqual(inputs[0].attributes['aria-valuetext'],'Off');
buttons[1].onclick();assert.strictEqual(sent.at(-1).value,30);
render({...fixture,settings:[{...slider,value:58}]});const range=ids.get('card-test-cap').querySelectorAll('input')[0];
assert(!range.disabled);assert.strictEqual(range.attributes['aria-valuetext'],'58 fps');range.value=60;range.oninput();assert.strictEqual(range.attributes['aria-valuetext'],'60 fps');
// Diagnostic preview nodes remain intact on unchanged snapshots, including dialog focus.
const report={visible:true,files:[{name:'graphics.txt',size:12,text:'Sanitized text'}],bytes:12};
const reportState={...fixture,report:{...report,loggingHint:'Turn on Detailed logging, reproduce the problem, then create the report.'},status:'Package preview ready'};render(reportState);
assert(!ids.get('logging-hint').hidden&&ids.get('logging-hint').textContent.includes('Detailed logging'));

const reportSummary=ids.get('report-summary-graphics.txt'),fileDetails=ids.get('report-file-graphics.txt');fileDetails.open=true;reportSummary.focus();
render(reportState);assert.strictEqual(ids.get('report-summary-graphics.txt'),reportSummary,'unchanged report rebuilt');assert.strictEqual(document.activeElement,reportSummary);assert(fileDetails.open);
render({...reportState,report:{...report,files:[{...report.files[0],text:'Updated sanitized text'}]}});
assert.notStrictEqual(ids.get('report-summary-graphics.txt'),reportSummary);assert(ids.get('report-file-graphics.txt').open,'updated report disclosure collapsed');assert.strictEqual(document.activeElement,ids.get('report-summary-graphics.txt'),'updated report lost dialog focus');
// Requirement copy: one deduplicated line per card naming only the choices affected.
const needsCard={id:'test-needs',title:'Needs fixture',category:'Surfaces',value:0,apply:'Applies after saving',choices:[
 {value:0,label:'Off',apply:'Applies after saving',reason:'',requirements:''},
 {value:1,label:'Glossy',apply:'Applies after saving',reason:'Needs Ray-tracing detail: Solid surfaces',requirements:'Hardware ray tracing; Ray-tracing detail: Solid surfaces'},
 {value:2,label:'Rough',apply:'Applies after saving',reason:'Needs Ray-tracing detail: Solid surfaces',requirements:'Hardware ray tracing; Ray-tracing detail: Solid surfaces'},
 {value:3,label:'Water',apply:'Restart the game to apply',reason:'Needs Water lighting: Reflections',requirements:'Water lighting: Reflections'}]};
render({...fixture,settings:[...fixture.settings,needsCard]});ids.get('nav-Surfaces').onclick();
const needLine=ids.get('needs-test-needs').children[1].textContent;
assert.strictEqual(needLine,'Glossy and Rough need Ray-tracing detail: Solid surfaces\nWater needs Water lighting: Reflections');
const needDetails=all(ids.get('card-test-needs')).filter(e=>e.className==='detail-copy').map(e=>e.textContent).join('\n');
assert(needDetails.includes('Glossy, Rough: Hardware ray tracing; Ray-tracing detail: Solid surfaces')&&needDetails.includes('Water: Water lighting: Reflections'));
// Release opt-in: switch off by default, page and warning only while on, Off returns to a qualified page.
render(fixture);assert(!ids.get('nav-Experimental')||!ids.get('navigation').children.includes(ids.get('nav-Experimental')));
const sw=ids.get('experimental-switch');assert(sw&&sw.type==='checkbox'&&!sw.checked);sw.checked=true;sw.onchange();assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'experimental',value:true});
const opt={...fixture.settings.find(s=>s.choices.length===2),id:'water_surface_fresnel',title:'Water interface Fresnel',category:'Experimental',description:'Experimental: adjusts reflections at the water surface.',experimental:true,developer:false};
opt.choices=[{...opt.choices[0]},{...opt.choices[1],label:'On (unqualified)'}];
render({...fixture,experimental:true,settings:[...fixture.settings,opt]});
assert(ids.get('navigation').children.includes(ids.get('nav-Experimental')));ids.get('nav-Experimental').onclick();
assert(all(ids.get('section-content')).some(e=>e.textContent==='Not fully tested. May change the image or cost performance.'));
assert(all(ids.get('card-water_surface_fresnel')).some(e=>e.textContent==='Adjusts reflections at the water surface.'));
assert.strictEqual(ids.get('control-water_surface_fresnel-0').type,'checkbox','unqualified marker hidden; Off/On is a switch');
ids.get('experimental-switch').checked=false;ids.get('experimental-switch').onchange();assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'experimental',value:false});
render(fixture);assert(!all(ids.get('section-content')).some(e=>e.id==='card-water_surface_fresnel'));
// Preparation (docs/RTX_HAIR_ROADMAP.md 2b): one place, Home. Help & support has no second copy of its rows or actions.
{const prepState={items:[{id:'hair',title:'RTX Hair',line:'3 of 5 styles ready, 0.1 GB on disk.',state:'running',progress:0.6},{id:'shaders',title:'Shaders',line:'Ready.',state:'done'}],whilePlaying:false,paused:false,pausable:true};
 render({...fixture,preparation:prepState});ids.get('nav-Support').onclick();
 assert(!all(ids.get('section-content')).some(e=>e.id&&/^prep-/.test(e.id)),'no preparation on Help & support');
 const grid=ids.get('section-content').children.find(e=>e.className==='grid support');assert.strictEqual(grid.children[0].id,'support-setup','game and graphics first');}
// No user text says "Strand hair": the feature is RTX Hair everywhere.
for(const page of ['Home','Lighting','Hotkeys','Support']){render(fixture);ids.get('nav-'+page).onclick();assert(!all(ids.get('section-content')).some(e=>typeof e.textContent==='string'&&/strand hair/i.test(e.textContent)),'Strand hair on '+page);}
// Apply badges: three kinds only.
for(const page of ['Lighting','Surfaces','Performance','DLSS 5','Support']){render(fixture);ids.get('nav-'+page).onclick();
 for(const b of all(ids.get('section-content')).filter(e=>/^badge/.test(e.className||'')))assert(['Applies now','Applies after saving','Restart to apply','May need a restart'].includes(b.textContent),page+' badge '+b.textContent);}
// Report preview: a save entry is listed with its note and size, never with a contents view.
render({...fixture,report:{visible:true,privacy:'p',bytes:12,files:[{name:'CONTENTS.txt',size:4,text:'list'},{name:'saves/Tav/Tav.lsv',size:8,text:'',note:'Your save',binary:true}]}});
assert(ids.get('report-file-'+encodeURIComponent('CONTENTS.txt'))&&!ids.get('report-file-'+encodeURIComponent('saves/Tav/Tav.lsv')),'no contents view for a save');
assert(all(ids.get('report-preview')).some(e=>e.textContent==='Your save'),'save note shown');
ids.get('prepare-report').onclick();assert.strictEqual(JSON.parse(JSON.stringify(sent.at(-1))).save,false,'latest save unticked by default');
render({...fixture,developer:true});assert(!ids.get('navigation').children.some(e=>e.className==='nav-foot'&&e.children.some(c=>c.className==='switch-row')),'developer mode keeps its own disclosure');
console.log('Renderer: Home dashboard, RTX Hair conversion, Hotkeys, category reachability, requirements, costs, focus, native controls, presets and developer authorization pass.');
