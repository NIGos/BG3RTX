// Execute the shipped renderer with a minimal DOM. No browser, network or game.
// stdin: the bridge's state JSON (release). argv[2]: the same with the experimental opt-in on (optional).
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const fixture=JSON.parse(fs.readFileSync(0,'utf8'));
const optIn=process.argv[2]?JSON.parse(fs.readFileSync(process.argv[2],'utf8')):null;
function dom() {
 const ids=new Map();
 class Element {
  constructor(tag){this.tagName=tag;this.children=[];this.style={setProperty(){}};this.attributes={};this.open=false;this.textContent='';this.className='';}
  set id(v){this._id=v;ids.set(v,this);} get id(){return this._id;}
  append(...items){this.children.push(...items.filter(x=>x!==null&&x!==undefined));} replaceChildren(...items){this.children=[];this.append(...items);}
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
 return {ids,document,Element};
}
const {ids,document}=dom();
let receive;const sent=[];
const context=vm.createContext({document,navigator:{},location:{search:'',protocol:'file:'},URLSearchParams,window:{chrome:{webview:{postMessage:m=>sent.push(m),addEventListener:(_,fn)=>receive=fn}}}});
vm.runInContext(fs.readFileSync('tools/launcher/ui/app.js','utf8'),context);
const render=s=>receive({data:{...s,type:'state'}});
const all=e=>[e,...e.children.flatMap(c=>typeof c==='object'?all(c):[])];
const msg=()=>JSON.parse(JSON.stringify(sent.at(-1)));
const page=()=>all(ids.get('section-content'));
const onPage=id=>!!ids.get(id)&&page().includes(ids.get(id));
const inNav=id=>ids.get('navigation').children.includes(ids.get('nav-'+id));
const go=id=>ids.get('nav-'+id).onclick();
const text=e=>all(e).map(x=>x.textContent||'').join('\n');
const act=id=>{const e=ids.get(id);assert(e,id+' missing');if(e.type==='checkbox'){e.checked=!e.checked;e.onchange();}else e.onclick();return msg();};
render(fixture);

// Shell: the same on every page. Navigation in a fixed order, Play and the version in the sidebar, preset in the header.
{
 assert.deepStrictEqual(ids.get('navigation').children.filter(e=>e.id).map(e=>e.id),['Home','Lighting','Surfaces','Performance','DLSS 5','Experimental','Hotkeys','Support'].map(x=>'nav-'+x));
 assert.strictEqual(ids.get('nav-Home').attributes['aria-current'],'page','Home is the default page');
 assert(!inNav('Developer')&&!inNav('Advanced')&&!inNav('Memory'),'release has no developer pages');
 for(const p of ['Home','Lighting','Surfaces','Performance','DLSS 5','Experimental','Hotkeys','Support']){
  go(p);
  assert.strictEqual(ids.get('nav-'+p).attributes['aria-current'],'page');
  assert(ids.get('page-title').textContent&&ids.get('page-lead').textContent,p+' has a title and a lead');
  assert(!ids.get('header').hidden&&!ids.get('play').hidden,'header and Play on '+p);
  assert.strictEqual(ids.get('play').disabled,!fixture.canPlay);
  assert(ids.get('hero-version').textContent.includes(fixture.version),'version in the sidebar on '+p);
  for(let i=0;i<4;i++){ids.get('preset-'+i).onclick();assert.deepStrictEqual(msg(),{type:'preset',value:i});}
  assert.strictEqual(ids.get('body-grid').className,['Home','Hotkeys','Support'].includes(p)?'body no-detail':'body',p+' detail panel');
 }
 ids.get('play').onclick();assert.deepStrictEqual(msg(),{type:'play'});
 go('Home');
 // Preset state: the preset of the file is pressed; a changed one is "Custom, based on X" with a reset.
 assert.strictEqual(ids.get('preset-'+fixture.preset).attributes['aria-pressed'],'true');
 render({...fixture,custom:true});
 assert.strictEqual(ids.get('preset-'+fixture.preset).className,'base');assert(/Custom/.test(text(ids.get('preset-note'))));
 ids.get('preset-reset').onclick();assert.deepStrictEqual(msg(),{type:'preset',value:fixture.preset});
 render(fixture);
}
// Instant apply: no Save, no Discard, no review dialog. A click sends its change; the bridge answers with what changed.
{
 for(const id of ['save','discard','confirmation','confirm-preview','cancel-preview','home-save'])assert(!ids.has(id),id+' belongs to the old Save flow');
 const source=fs.readFileSync('tools/launcher/ui/app.js','utf8');
 for(const gone of ["send('save'","send('discard'","send('confirm'","send('cancel'","unsavedCount","state.preview"])assert(!source.includes(gone),gone+' left in app.js');
 // Nothing to say: the status strip is hidden. A message from the bridge shows it, without Undo.
 render({...fixture,status:''});assert.strictEqual(ids.get('changes').className,'idle','status strip out of sight when there is nothing to say');
 render({...fixture,status:'Report saved on your PC.'});assert(ids.get('changes').className===''&&ids.get('status').textContent==='Report saved on your PC.'&&ids.get('undo').hidden&&ids.get('dismiss').hidden);
 // A change: what changed, what changed with it, Undo and Dismiss; the same list sits in the row that was clicked.
 const notice={text:'RTX Hair: On → Off',also:['Strand hair edge resolve: Analytic coverage → Off'],anchor:'exp_hair_groom_primary',undo:true};
 render({...fixture,status:'',notice});
 assert.strictEqual(ids.get('status').textContent,'RTX Hair: On → Off. Also changed: Strand hair edge resolve: Analytic coverage → Off.');
 assert(ids.get('changes').className===''&&!ids.get('undo').hidden&&!ids.get('dismiss').hidden);
 ids.get('undo').onclick();assert.deepStrictEqual(msg(),{type:'undo'});
 ids.get('dismiss').onclick();assert.deepStrictEqual(msg(),{type:'dismiss'});
 go('Lighting');
 assert(all(ids.get('card-exp_hair_groom_primary')).includes(ids.get('row-also')),'the dependency note sits in the row that was clicked');
 assert(/Also changed: Strand hair edge resolve: Analytic coverage → Off/.test(text(ids.get('row-also')))&&ids.get('row-also').attributes.role==='status');
 ids.get('row-undo').onclick();assert.deepStrictEqual(msg(),{type:'undo'});
 // A change with nothing else: no note in the row; Undo only while the bridge offers it.
 render({...fixture,notice:{text:'Reflections: Rough → Standard',also:[],anchor:'reflections',undo:true}});go('Surfaces');
 assert(!onPage('row-also')&&!ids.get('undo').hidden);
 render({...fixture,notice:{text:'Undone',also:[],anchor:'',undo:false}});assert(ids.get('undo').hidden&&ids.get('status').textContent==='Undone.');
 // A failed write: a red banner on every page with the host's message, Retry and Dismiss; nothing else changes.
 const failed={...fixture,status:'',writeError:{text:'Settings not saved. Nothing was changed.\nWindows error 32',retry:true}};
 for(const p of ['Home','Lighting','Support']){render(failed);go(p);
  assert(ids.get('warnings').children.includes(ids.get('write-error'))&&ids.get('write-error').attributes.role==='alert',p+' shows the failed write');
  assert(/Windows error 32/.test(text(ids.get('write-error'))));}
 ids.get('retry-write').onclick();assert.deepStrictEqual(msg(),{type:'retry'});
 ids.get('dismiss-error').onclick();assert.deepStrictEqual(msg(),{type:'dismiss'});
 go('Home');assert(onPage('home-status-error'));ids.get('home-retry').onclick();assert.deepStrictEqual(msg(),{type:'retry'});
 render({...failed,writeError:{text:'Review mode cannot write settings.',retry:false}});assert(!ids.get('warnings').children.some(e=>all(e).some(x=>x.id==='retry-write')),'no Retry when nothing waits');
 // Restart indicator: calm, on every page, with the names; the rows that wait carry "Restart pending".
 const waiting={...fixture,status:'',restartNeeded:true,restartCount:2,restartNames:['Ray-tracing detail','Water lighting'],restartBanner:'Restart the game to apply: Ray-tracing detail, Water lighting.'};
 for(const p of ['Home','Lighting','Surfaces','Hotkeys']){render(waiting);go(p);
  assert(ids.get('warnings').children.includes(ids.get('restart-banner'))&&ids.get('restart-banner').attributes.role==='status',p+' restart indicator');
  assert.strictEqual(all(ids.get('restart-banner')).find(e=>e.tagName==='span'&&e.className==='').textContent,'Restart the game to apply: Ray-tracing detail, Water lighting.');}
 go('Lighting');assert(all(ids.get('card-rt_geometry')).some(e=>e.className==='badge pending'&&e.textContent==='Restart pending'));
 assert(!all(ids.get('card-rr')).some(e=>e.className==='badge pending'));
 go('Surfaces');assert(all(ids.get('card-exp_water')).some(e=>e.className==='badge pending'));
 go('Home');assert(/Restart the game to apply: Ray-tracing detail, Water lighting\./.test(text(ids.get('home-status-restart'))));
 render(fixture);
}
// Home: headline, only what needs attention, preparation, the preset and the keys. No settings of its own.
{
 for(const id of ['home-play-card','home-attention','home-preparation','home-shaders','home-preset','home-keys'])assert(onPage(id),id+' missing on Home');
 assert(!page().some(e=>e.id&&/^control-/.test(e.id)),'no setting controls on Home');
 // The bridge fixture has no game folder and no GPU: both are named. With them, the list is calm.
 assert(onPage('home-status-game')&&onPage('home-status-gpu')&&!onPage('home-status-none'));
 const calm={...fixture,gamePath:'C:/Games/BG3',canPlay:true,modStatus:'Mod files present',compatibility:{...fixture.compatibility,gpu:'NVIDIA GeForce RTX 5090',vram:'32 GB',warnings:[]}};
 render(calm);
 assert(onPage('home-status-none')&&/Nothing needs attention/.test(text(ids.get('home-attention'))),'calm when nothing is wrong');
 assert(/Ready to play/.test(text(ids.get('home-play-card')))&&!ids.get('play').disabled&&!ids.get('warnings').children.length);
 assert(/diagnostic|report/i.test(ids.get('home-report-open').textContent));
 ids.get('home-report-open').onclick();assert.deepStrictEqual(msg(),{type:'report'});
 for(const [id,v] of Object.entries(fixture.shortcuts))assert(all(ids.get('home-keys')).some(e=>e.tagName==='kbd'&&e.textContent===v),id+' on Home');
 ids.get('home-keys-change').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');go('Home');
 // Problems: game folder, restart; each with its fix where there is one. The navigation marks Home.
 render({...calm,restartNeeded:true,restartBanner:'Restart the game to apply 2 changes',gamePath:'',canPlay:false});
 assert(!onPage('home-status-none'));
 assert(ids.get('home-status-restart').children[1].textContent.startsWith('Restart the game to apply 2 changes'));
 ids.get('home-browse').onclick();assert.deepStrictEqual(msg(),{type:'browseGame'});
 assert(all(ids.get('nav-Home')).some(e=>e.className==='nav-count'),'Home is marked while Play is blocked');
 assert(/game folder/i.test(ids.get('play-note').textContent)&&ids.get('play').disabled);
 assert(/Game folder needed/.test(text(ids.get('home-play-card'))));
 assert(!ids.get('warnings').children.some(e=>e.id!=='restart-banner'),'Home shows the problem once, in its list');
 go('Lighting');ids.get('notice-browse').onclick();assert.deepStrictEqual(msg(),{type:'browseGame'});
 assert(ids.get('warnings').children.includes(ids.get('restart-banner')),'restart banner on every page');go('Home');
 render({...calm,compatibility:{...calm.compatibility,gpu:'Graphics unknown',warnings:['Graphics compatibility could not be checked.']}});
 ids.get('home-recheck').onclick();assert.deepStrictEqual(msg(),{type:'recheck'});
 // Preparation: RTX Hair numbers, progress with ETA, one action; source, granny2.dll and schedule; Shaders.
 render(fixture);
 assert(!onPage('prep-pause'),'no hair actions without preparation data');
 const prep={items:[{id:'hair',title:'RTX Hair',line:'3 of 5 styles ready, 0.1 GB on disk.',detail:'Converting styles seen in game, about 2 min left.',progress:0.6,state:'running'},
  {id:'shaders',title:'Shaders',line:'Preparing shaders: 4 of 9.',detail:'Your graphics driver still builds the first ray tracing pipeline in game, about a minute.',progress:0.4,state:'running'}],
  whilePlaying:false,paused:false,pausable:true,convertAll:false,hair:{ready:3,captured:4,needed:5,finished:3,bytes:5e7,etaSeconds:120,source:'seen'},
  granny:{path:'',ok:false},convertAllReason:'Needs granny2.dll: choose it below.'};
 render({...calm,preparation:prep});
 assert(all(ids.get('home-hair')).includes(ids.get('prep-hair'))&&all(ids.get('home-shaders')).includes(ids.get('prep-shaders')));
 assert(all(ids.get('home-preparation')).includes(ids.get('home-hair')));
 assert(onPage('home-status-prep')&&/You can play now/.test(text(ids.get('home-status-prep'))),'running work is named, and never blocks Play');
 assert(all(ids.get('nav-Home')).some(e=>e.className==='nav-dot'),'Home is marked while work runs');
 assert.strictEqual(ids.get('hair-stat-ready').children[0].textContent,'3');assert.strictEqual(ids.get('hair-stat-captured').children[0].textContent,'4');
 assert.strictEqual(ids.get('hair-stat-total').children[0].textContent,'–','unknown total shown as a dash');
 assert.strictEqual(ids.get('hair-stat-disk').children[0].textContent,'50 MB');
 assert(all(ids.get('prep-hair')).some(e=>e.textContent==='60% · about 2 min left'),'progress with ETA');
 assert(all(ids.get('home-hair')).some(e=>e.textContent==='3 of 5 styles ready, 0.1 GB on disk.'),'the host line');
 assert(all(ids.get('home-shaders')).some(e=>e.textContent==='Preparing shaders: 4 of 9.')&&all(ids.get('home-shaders')).some(e=>/first ray tracing pipeline/.test(e.textContent)));
 assert(ids.get('prep-progress-shaders'),'shader progress bar');
 assert.strictEqual(ids.get('prep-state-hair').textContent,'Working');
 ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'pause',value:0});
 assert.strictEqual(ids.get('prep-options').tagName,'details');assert(all(ids.get('prep-options')).includes(ids.get('prep-when-1')),'options under one disclosure');
 ids.get('prep-when-1').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'whilePlaying',value:1});
 ids.get('prep-when-0').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'whilePlaying',value:0});
 // Convert all styles: disabled with its one-line reason until granny2.dll is set; the picker is always there.
 assert(ids.get('prep-source-1').disabled&&ids.get('prep-source-1').title===prep.convertAllReason);
 assert(!ids.get('prep-source-reason').hidden&&ids.get('prep-source-reason').children[1].textContent===prep.convertAllReason);
 assert.strictEqual(ids.get('prep-source-0').attributes['aria-pressed'],'true');
 ids.get('prep-granny-browse').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'browseGranny',value:0});
 assert.strictEqual(ids.get('prep-granny-help').textContent,"Convert all styles needs granny2.dll, which BG3RTX cannot include. If you have LSLib's ExportTool (github.com/Norbyte/lslib, Releases), point to the granny2.dll in its folder.");
 // The options disclosure stays as the player left it across bridge refreshes.
 ids.get('prep-options').open=true;render({...calm,preparation:prep});assert(ids.get('prep-options').open,'conversion options collapsed on refresh');
 // The host's line wins; --all progress per tier is listed in conversion order.
 render({...calm,preparation:{...prep,grannyHelp:'Host line.',hair:{...prep.hair,source:'all',total:1217,tiers:[{tier:'companion',total:6,finished:6},{tier:'player',total:432,finished:400}]}}});
 assert.strictEqual(ids.get('prep-granny-help').textContent,'Host line.');
 assert.strictEqual(ids.get('prep-hair-tiers').textContent,'Companions 6/6 · Character creation 400/432');
 render({...calm,preparation:{...prep,granny:{path:'C:\\Tools\\granny2.dll',ok:true},convertAllReason:''}});
 assert(!ids.get('prep-source-1').disabled&&ids.get('prep-source-reason').hidden);
 ids.get('prep-source-1').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'convertAll',value:1});
 ids.get('prep-granny-clear').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'clearGranny',value:0});
 render({...calm,preparation:{...prep,paused:true}});ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'resume',value:0});
 // A stopped run: Try again sends start; the last error is shown and named under Needs attention.
 render({...calm,preparation:{...prep,items:[{...prep.items[0],state:'failed'},prep.items[1]],hair:{...prep.hair,error:'granny2.dll could not be loaded.'}}});
 assert.strictEqual(ids.get('prep-pause').textContent,'Try again');ids.get('prep-pause').onclick();assert.deepStrictEqual(msg(),{type:'prep',action:'start',value:0});
 assert.strictEqual(ids.get('prep-hair-error').textContent,'Last error: granny2.dll could not be loaded.');
 assert.strictEqual(ids.get('prep-state-hair').textContent,'Stopped');assert(onPage('home-status-hair'));
 // Tolerant of an older host: no hair facts, no granny, no reason.
 render({...calm,preparation:{items:prep.items,whilePlaying:false,paused:false,pausable:true}});
 assert(all(ids.get('home-hair')).some(e=>e.textContent==='3 of 5 styles ready, 0.1 GB on disk.'),'host line without numbers');
 assert(!ids.get('prep-source-1').disabled,'no reason, no block');
 // Unavailable converter: the line, no actions.
 render({...calm,preparation:{...prep,items:[{id:'hair',title:'RTX Hair',line:'Not available: the hair converter is not installed.',state:'unavailable'}]}});
 assert(!all(ids.get('home-hair')).some(e=>e.id&&/^prep-(pause|source|when|granny)/.test(e.id)),'no actions without a converter');
 render(fixture);
}
// Every setting the bridge sends is reachable, once, on the page of its category, with its control and requirements.
const pageOf=s=>s.category;
for(const category of new Set(fixture.settings.map(pageOf))){
 assert(inNav(category),category+' has no page');go(category);
 const rows=page().filter(e=>e.tagName==='article'&&e.id);
 for(const s of fixture.settings.filter(s=>pageOf(s)===category)){
  const awaitingOnly=s.id!=='dlss5_nr'&&s.choices.length>1&&s.choices.slice(1).every(ch=>/experimental|awaiting/.test(ch.maturity));
  if(awaitingOnly){assert(!rows.some(c=>c.id==='card-'+s.id),s.id+' belongs on Experimental');continue;}
  assert.strictEqual(rows.filter(c=>c.id==='card-'+s.id).length,1,s.id+' unreachable or repeated');
  const row=rows.find(c=>c.id==='card-'+s.id);
  assert(!all(row).some(e=>/GPU cost:|VRAM: Unknown/.test(e.textContent)),s.id+' has developer cost text');
  const controls=row.querySelectorAll('button,input').filter(e=>e.id&&/^control-/.test(e.id));assert(controls.length,s.id+' has no control');
  for(const control of controls)assert(ids.has(control.attributes['aria-describedby']),s.id+' control description');
  // Unmet requirements once per row; the full list is in the detail.
  const needs=ids.get('needs-'+s.id),reasons=needs.children.map(c=>c.textContent||'').join('');
  assert.strictEqual(needs.hidden,!s.choices.some(ch=>ch.reason),s.id+' needs visibility');
  for(const ch of s.choices){if(ch.reason)assert(reasons.includes(ch.reason.replace(/^Needs /,'')),s.id+' reason');}
  const detailText=all(row).filter(e=>e.className==='detail-copy').map(e=>e.textContent).join('\n');
  for(const ch of s.choices)if(ch.requirements)assert(detailText.includes(ch.requirements),s.id+' requirements in the detail');
  for(const heading of ['What it does','GPU cost','Takes effect'])assert(all(row).some(e=>e.className==='detail-heading'&&e.textContent===heading),s.id+' detail heading '+heading);
 }
}
// Lighting: groups in order, children under their parent, RTX Hair with its two sub-controls in the Hair group.
go('Lighting');
assert.deepStrictEqual(page().filter(e=>e.className==='group-title').map(e=>e.textContent),['Ray tracing','Shadows','Bounced light','Hair']);
assert.deepStrictEqual(page().filter(e=>e.tagName==='article'&&e.id).map(e=>e.id),
 ['rt_geometry','rr','rt_intensity','sun_shadows','local_shadows','box_lights','gi','exp_hidden_surface','exp_native_coverage','exp_hair_groom_primary','hair_distant_detail','hair_motion_stability'].map(id=>'card-'+id));
for(const id of ['box_lights','exp_hidden_surface','hair_distant_detail','hair_motion_stability'])assert(/\bchild\b/.test(ids.get('card-'+id).className),id+' is a child row');
{
 // A child whose parent is off is dimmed and says why; its control still works.
 const hair=fixture.settings.find(s=>s.id==='exp_hair_groom_primary');
 const withHair=v=>({...fixture,settings:fixture.settings.map(s=>s.id===hair.id?{...s,value:v}:s)});
 render(withHair(0));assert(/parent-off/.test(ids.get('card-hair_distant_detail').className)&&/Only has an effect while RTX Hair is on/.test(text(ids.get('card-hair_distant_detail'))));
 render(withHair(1));assert(!/parent-off/.test(ids.get('card-hair_distant_detail').className));
 assert(all(ids.get('card-exp_hair_groom_primary')).some(e=>e.className==='badge beta'),'RTX Hair is marked Beta');
 const fromRow=act('control-exp_hair_groom_primary-0');assert.deepStrictEqual(fromRow,{type:'set',id:hair.id,value:0});
 // Its hotkey (when the bridge has the binding) links to Hotkeys.
 render({...fixture,shortcuts:{...fixture.shortcuts,hotkey_rtx_hair:'Ctrl+Shift+H'}});
 const hk=ids.get('card-hotkey-exp_hair_groom_primary');assert(hk&&all(hk).some(e=>e.tagName==='kbd'&&e.textContent==='Ctrl+Shift+H'));
 ids.get('card-hotkey-change-exp_hair_groom_primary').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');
 assert(ids.get('hotkey_rtx_hair'),'RTX Hair hotkey on the Hotkeys page');
 render(fixture);go('Lighting');
}
// Keyboard: a group of choices is one Tab stop (the current choice); arrows move focus between choices without
// writing; Home and End jump; a disabled choice is skipped. The navigation works the same with Up and Down.
{
 const gi=fixture.settings.find(s=>s.id==='gi'),buttons=ids.get('card-gi').querySelectorAll('button').filter(b=>/^control-gi-/.test(b.id));
 assert.deepStrictEqual(buttons.map(b=>b.tabIndex),buttons.map((_,i)=>i===gi.value?0:-1),'one Tab stop per group');
 const key=k=>({key:k,preventDefault(){}});let before=sent.length;
 buttons[gi.value].focus();buttons[gi.value].onkeydown(key('ArrowRight'));
 assert.strictEqual(document.activeElement,buttons[(gi.value+1)%buttons.length]);
 document.activeElement.onkeydown(key('ArrowLeft'));assert.strictEqual(document.activeElement,buttons[gi.value]);
 buttons[0].onkeydown(key('ArrowLeft'));assert.strictEqual(document.activeElement,buttons[buttons.length-1],'wraps');
 buttons[1].onkeydown(key('Home'));assert.strictEqual(document.activeElement,buttons[0]);buttons[0].onkeydown(key('End'));assert.strictEqual(document.activeElement,buttons[buttons.length-1]);
 assert.strictEqual(sent.length,before,'moving between choices writes nothing');
 render({...fixture,settings:fixture.settings.map(s=>s.id==='gi'?{...s,value:0,choices:s.choices.map((c,i)=>({...c,enabled:i!==1}))}:s)});
 const limited=ids.get('card-gi').querySelectorAll('button').filter(b=>/^control-gi-/.test(b.id));
 limited[0].onkeydown(key('ArrowRight'));assert.strictEqual(document.activeElement,limited[2],'a disabled choice is skipped');
 render(fixture);
 const presets=ids.get('presets').children;assert.deepStrictEqual(presets.map(b=>b.tabIndex),presets.map((_,i)=>i===fixture.preset?0:-1));
 const nav=ids.get('navigation').children.filter(e=>e.tagName==='button');
 assert.deepStrictEqual(nav.map(b=>b.tabIndex),nav.map(b=>b.id==='nav-Lighting'?0:-1));
 ids.get('nav-Lighting').onkeydown(key('ArrowDown'));assert.strictEqual(document.activeElement,ids.get('nav-Surfaces'));
 ids.get('nav-Surfaces').onkeydown(key('ArrowRight'));assert.strictEqual(document.activeElement,ids.get('nav-Surfaces'),'the sidebar uses Up and Down only');
 assert(/Ctrl\+Z/.test(ids.get('undo').title));
 document.activeElement=null;
}
// The detail panel shows the selected row; selecting another row does not rebuild the page.
{
 const first=ids.get('card-rt_geometry');assert(/selected/.test(first.className),'the first row is selected');
 assert(all(ids.get('detail')).some(e=>e.className==='detail-title'&&e.textContent==='Ray-tracing detail'));
 const sun=ids.get('card-sun_shadows');sun.onclick();
 assert(/selected/.test(sun.className)&&!/selected/.test(first.className)&&ids.get('card-sun_shadows')===sun);
 assert(all(ids.get('detail')).some(e=>e.className==='detail-title'&&e.textContent==='Sun shadows'));
 render(fixture);assert(/selected/.test(ids.get('card-sun_shadows').className),'selection lost on bridge update');
}
// Restart badge: only where a change from the current value waits for a game restart, with its direction.
{
 const badgeOf=id=>all(ids.get('card-'+id)).find(e=>e.className==='badge restart')?.textContent;
 const set=(id,v)=>render({...fixture,settings:fixture.settings.map(s=>s.id===id?{...s,value:v,choices:s.choices.map(c=>({...c}))}:s)});
 assert.strictEqual(badgeOf('rt_geometry'),'Restart');
 assert.strictEqual(badgeOf('rr'),undefined,'a live setting has no apply badge');
 assert.strictEqual(badgeOf('sun_shadows'),undefined);
 go('Performance');
 const fg=fixture.settings.find(s=>s.id==='frame_generation');
 assert.strictEqual(badgeOf('frame_generation'),fg.value===0?'Restart':'Restart for Off');
 go('Lighting');
 const custom={id:'test-toggle',title:'Toggle fixture',category:'Lighting',value:0,apply:'Applies at once',description:'x',choices:[{value:0,label:'Off',apply:'Applies at once',enabled:true},{value:1,label:'On',apply:'Restart the game to apply',enabled:true}]};
 render({...fixture,settings:[...fixture.settings,custom]});assert.strictEqual(badgeOf('test-toggle'),'Restart to turn on');
 assert(all(ids.get('card-test-toggle')).length&&page().some(e=>e.className==='group-title'&&e.textContent==='More'),'an unlisted setting gets a last group');
 render({...fixture,settings:[...fixture.settings,{...custom,value:1,choices:[custom.choices[0],{...custom.choices[1],apply:'Applies at once'}]}]});assert.strictEqual(badgeOf('test-toggle'),undefined);
 render(fixture);
}
// Performance: Tracing speed is collapsed by default and remembers being opened.
go('Performance');
{
 assert.deepStrictEqual(page().filter(e=>e.className==='group-title').map(e=>e.textContent),['Frame rate','Tracing speed']);
 const d=ids.get('group-Tracing-speed');assert(d&&d.tagName==='details'&&!d.open,'Tracing speed starts collapsed');
 assert(all(d).includes(ids.get('card-exp_gi_rt_pipeline'))&&/\bchild\b/.test(ids.get('card-exp_gi_rt_pipeline_reorder').className));
 d.open=true;render(fixture);assert(ids.get('group-Tracing-speed').open,'disclosure collapsed on bridge update');
 assert(onPage('experimental-pointer')&&/hidden/.test(text(ids.get('experimental-pointer'))),'the page says where its experimental options are');
 ids.get('experimental-open').onclick();assert.strictEqual(ids.get('nav-Experimental').attributes['aria-current'],'page');
}
go('DLSS 5');
assert(ids.get('dlss5-runtime-browse'));ids.get('dlss5-runtime-browse').onclick();assert.deepStrictEqual(msg(),{type:'browseDlss5Runtime'});
assert(all(ids.get('card-dlss5_nr')).some(e=>e.className==='badge exp'),'DLSS 5 carries the Experimental badge');
assert(!/experimental/i.test(all(ids.get('card-dlss5_nr')).find(e=>e.className==='row-title').textContent),'the badge says experimental, not the label');
assert(all(ids.get('card-dlss5_nr')).includes(ids.get('card-hotkey-dlss5_nr')),'the DLSS 5 row shows its hotkey');
ids.get('card-hotkey-change-dlss5_nr').onclick();assert.strictEqual(ids.get('nav-Hotkeys').attributes['aria-current'],'page');
// Hotkeys: every binding the bridge sends, in one place.
for(const id of Object.keys(fixture.shortcuts))assert(onPage('hotkey-row-'+id),id+' not on Hotkeys');
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
assert(/reserved for a developer tool/.test(ids.get('hotkey_dlss5-help').textContent),'the reserved key says why');
nrKey.onkeydown({...event('N'),ctrlKey:false,shiftKey:false,altKey:true});
assert.deepStrictEqual(msg(),{type:'shortcut',id:'hotkey_dlss5',value:'Alt+N'});
assert(ids.get('hotkey_rt_toggle'));
const reset=ids.get('hotkey-reset-hotkey_rt_toggle');assert.strictEqual(reset.textContent,'Reset to '+fixture.shortcutDefaults.hotkey_rt_toggle);reset.onclick();
assert.deepStrictEqual(msg(),{type:'resetShortcut',id:'hotkey_rt_toggle'});
ids.get('hotkey_rt_toggle').onkeydown(event('Delete'));assert.strictEqual(sent.at(-1).value,'None');
ids.get('hotkey-none-hotkey_rtx_hair').onclick();assert.deepStrictEqual(msg(),{type:'shortcut',id:'hotkey_rtx_hair',value:'None'});
go('Lighting');
assert(!ids.has('summary')&&!ids.has('report'),'duplicate summary or report entry');
const rr=ids.get('control-rr-0');assert(rr.type==='checkbox'&&rr.attributes['aria-label']&&rr.attributes.role==='switch');
rr.focus();render(fixture);assert.strictEqual(document.activeElement,ids.get(rr.id),'focus lost on bridge update');
const checkbox=ids.get(rr.id);let clicked=false;checkbox.click=()=>clicked=true;checkbox.onkeydown({key:'Enter',preventDefault(){}});assert(clicked);
assert(!page().some(e=>e.id==='experimental-summary'));
// Help & support: game folder and graphics, reports with Detailed logging, tips, version.
go('Support');
assert.deepStrictEqual(page().filter(e=>e.className==='group-title').map(e=>e.textContent),['Game and graphics','Problem reports','Tips','About']);
assert(page().some(e=>e.textContent.includes("In game, Ctrl+Shift+F9 switches all ray-traced lighting off and on")&&e.textContent.includes('Change it on Hotkeys')));
assert(page().some(e=>e.textContent==='Video memory'));
assert(page().some(e=>e.textContent==='Compatibility'));
assert(ids.get('support-report')&&ids.get('support-browse')&&ids.get('support-recheck'));
ids.get('support-report').onclick();assert.deepStrictEqual(msg(),{type:'report'});
ids.get('support-browse').onclick();assert.deepStrictEqual(msg(),{type:'browseGame'});
ids.get('support-recheck').onclick();assert.deepStrictEqual(msg(),{type:'recheck'});
assert(onPage('card-detailed_logging')&&ids.get('control-detailed_logging-0').type==='checkbox');
assert(all(ids.get('support-reports')).includes(ids.get('card-detailed_logging')),'Detailed logging sits with the report');
assert(text(ids.get('support-about')).includes(fixture.version),'version on Help & support');assert(ids.get('support-copy-version'));
assert(!page().some(e=>e.id&&/^prep-/.test(e.id)),'no preparation on Help & support');
const notices=page().find(e=>e.tagName==='a');
assert(notices&&notices.textContent==='Fonts and third-party notices');
notices.onclick({preventDefault(){}});assert.strictEqual(sent.at(-1).type,'notices');
render({...fixture,compatibility:{...fixture.compatibility,adapters:[{value:0,label:'A'},{value:1,label:'B'}],selected:-1}});
ids.get('support-adapter').value='1';ids.get('support-adapter').onchange();assert.deepStrictEqual(msg(),{type:'adapter',value:1});
// Explicit bridge authorization, rather than the presence of a developer row.
const dev={...fixture.settings[0],id:'test-dev',developer:true};
render({...fixture,settings:[...fixture.settings,dev]});assert(!inNav('Developer'));
render({...fixture,developer:true,settings:[...fixture.settings,dev]});
assert(inNav('Advanced')&&inNav('Developer')&&!inNav('Experimental'),'developer mode: its own pages, no release opt-in page');
go('Advanced');assert.strictEqual(ids.get('experimental-summary').tagName,'summary');go('Developer');assert(onPage('card-test-dev'));
assert(!ids.get('experimental-switch')||!onPage('experimental-switch'),'developer mode keeps its own disclosure');
render(fixture);
// Explicit Off has a pressed state; the inactive range stays inside its numeric bounds. The value is sent on release.
const slider={id:'test-cap',title:'Frame cap',category:'Performance',value:0,min:30,max:240,step:1,unit:'fps',hasOff:true,choices:[],apply:'Live'};
render({...fixture,settings:[...fixture.settings,slider]});go('Performance');
let cap=ids.get('card-test-cap'),inputs=cap.querySelectorAll('input'),buttons=cap.querySelectorAll('button');
assert.strictEqual(buttons[0].attributes['aria-pressed'],'true');assert.strictEqual(buttons[1].attributes['aria-pressed'],'false');
assert(inputs[0].disabled);assert.strictEqual(inputs[0].value,30);assert.strictEqual(inputs[0].attributes['aria-valuetext'],'Off');
buttons[1].onclick();assert.strictEqual(sent.at(-1).value,30);
render({...fixture,settings:[{...slider,value:58}]});const range=ids.get('card-test-cap').querySelectorAll('input')[0];
assert(!range.disabled);assert.strictEqual(range.attributes['aria-valuetext'],'58 fps');
count=sent.length;range.value=60;range.oninput();assert.strictEqual(range.attributes['aria-valuetext'],'60 fps');assert.strictEqual(sent.length,count,'dragging does not write');
range.onchange();assert.deepStrictEqual(msg(),{type:'set',id:'test-cap',value:60});
// Diagnostic preview nodes remain intact on unchanged snapshots, including dialog focus.
const report={visible:true,files:[{name:'graphics.txt',size:12,text:'Sanitized text'}],bytes:12};
const reportState={...fixture,report:{...report,loggingHint:'Turn on Detailed logging, reproduce the problem, then create the report.'},status:'Package preview ready'};render(reportState);
assert(!ids.get('logging-hint').hidden&&ids.get('logging-hint').textContent.includes('Detailed logging'));
const reportSummary=ids.get('report-summary-graphics.txt'),fileDetails=ids.get('report-file-graphics.txt');fileDetails.open=true;reportSummary.focus();
render(reportState);assert.strictEqual(ids.get('report-summary-graphics.txt'),reportSummary,'unchanged report rebuilt');assert.strictEqual(document.activeElement,reportSummary);assert(fileDetails.open);
render({...reportState,report:{...report,files:[{...report.files[0],text:'Updated sanitized text'}]}});
assert.notStrictEqual(ids.get('report-summary-graphics.txt'),reportSummary);assert(ids.get('report-file-graphics.txt').open,'updated report disclosure collapsed');assert.strictEqual(document.activeElement,ids.get('report-summary-graphics.txt'),'updated report lost dialog focus');
// Requirement copy: one deduplicated line per row naming only the choices affected.
const needsCard={id:'test-needs',title:'Needs fixture',category:'Surfaces',value:0,apply:'Applies at once',choices:[
 {value:0,label:'Off',apply:'Applies at once',reason:'',requirements:''},
 {value:1,label:'Glossy',apply:'Applies at once',reason:'Needs Ray-tracing detail: Solid surfaces',requirements:'Hardware ray tracing; Ray-tracing detail: Solid surfaces'},
 {value:2,label:'Rough',apply:'Applies at once',reason:'Needs Ray-tracing detail: Solid surfaces',requirements:'Hardware ray tracing; Ray-tracing detail: Solid surfaces'},
 {value:3,label:'Water',apply:'Restart the game to apply',reason:'Needs Water lighting: Reflections',requirements:'Water lighting: Reflections'}]};
render({...fixture,settings:[...fixture.settings,needsCard]});go('Surfaces');
const needLine=ids.get('needs-test-needs').children[1].textContent;
assert.strictEqual(needLine,'Glossy and Rough need Ray-tracing detail: Solid surfaces\nWater needs Water lighting: Reflections');
const needDetails=all(ids.get('card-test-needs')).filter(e=>e.className==='detail-copy').map(e=>e.textContent).join('\n');
assert(needDetails.includes('Glossy, Rough: Hardware ray tracing; Ray-tracing detail: Solid surfaces')&&needDetails.includes('Water: Water lighting: Reflections'));
// Experimental page: always in the release navigation, with its switch; the options appear only while it is on.
render(fixture);go('Experimental');
{
 const sw=ids.get('experimental-switch');assert(onPage('experimental-switch')&&sw.type==='checkbox'&&!sw.checked);
 assert(onPage('experimental-empty')&&!page().some(e=>e.tagName==='article'),'no options while the switch is off');
 sw.checked=true;sw.onchange();assert.deepStrictEqual(msg(),{type:'experimental',value:true});
 const opt={...fixture.settings.find(s=>s.choices.length===2),id:'water_surface_fresnel',title:'Water interface Fresnel',category:'Experimental',description:'Experimental: adjusts reflections at the water surface.',experimental:true,developer:false};
 opt.choices=[{...opt.choices[0]},{...opt.choices[1],label:'On (unqualified)'}];
 render({...fixture,experimental:true,settings:[...fixture.settings,opt]});
 assert(page().some(e=>e.textContent==='Not fully tested. Off in every preset. Turning this off hides them and sets them all back to Off.'));
 assert(all(ids.get('card-water_surface_fresnel')).some(e=>e.textContent==='Adjusts reflections at the water surface.'));
 assert(all(ids.get('card-water_surface_fresnel')).some(e=>e.className==='badge exp'));
 assert.strictEqual(ids.get('control-water_surface_fresnel-0').type,'checkbox','unqualified marker hidden; Off/On is a switch');
 // Low-latency scene (a Performance knob whose only upgrade is unqualified) sits here, not on Performance.
 assert(onPage('card-scene_handout'),'Low-latency scene on Experimental');
 assert.deepStrictEqual(page().filter(e=>e.className==='group-title').map(e=>e.textContent),['Performance','Water']);
 go('Performance');assert(!onPage('card-scene_handout'));assert(/Low-latency scene/.test(text(ids.get('experimental-pointer'))));
 go('Surfaces');assert(/1 experimental option: Water interface Fresnel/.test(text(ids.get('experimental-pointer'))));
 ids.get('experimental-open').onclick();assert(/selected/.test(ids.get('card-water_surface_fresnel').className),'the pointer opens the option it names');
 ids.get('experimental-switch').checked=false;ids.get('experimental-switch').onchange();assert.deepStrictEqual(msg(),{type:'experimental',value:false});
 render(fixture);assert(!onPage('card-water_surface_fresnel')&&!onPage('card-scene_handout'));
}
if(optIn){
 // The real opt-in state: every unqualified option of the release is on the Experimental page, in its group.
 render(optIn);go('Experimental');
 for(const s of optIn.settings.filter(s=>s.category==='Experimental'||s.id==='scene_handout'))assert(onPage('card-'+s.id),s.id+' not on Experimental');
 assert(!page().some(e=>e.className==='group-title'&&e.textContent==='More'),'every experimental option has its group');
 render(fixture);
}
// No user text says "Strand hair" on the qualified pages: the feature is RTX Hair.
for(const p of ['Home','Lighting','Hotkeys','Support']){render(fixture);go(p);assert(!page().some(e=>typeof e.textContent==='string'&&/strand hair/i.test(e.textContent)),'Strand hair on '+p);}
// Report preview: a save entry is listed with its note and size, never with a contents view.
render({...fixture,report:{visible:true,privacy:'p',bytes:12,files:[{name:'CONTENTS.txt',size:4,text:'list'},{name:'saves/Tav/Tav.lsv',size:8,text:'',note:'Your save',binary:true}]}});
assert(ids.get('report-file-'+encodeURIComponent('CONTENTS.txt'))&&!ids.get('report-file-'+encodeURIComponent('saves/Tav/Tav.lsv')),'no contents view for a save');
assert(all(ids.get('report-preview')).some(e=>e.textContent==='Your save'),'save note shown');
ids.get('prepare-report').onclick();assert.strictEqual(msg().save,false,'latest save unticked by default');
// The sample data of app.js (plain-browser preview, ?sample) matches the settings table: same ids, names, pages,
// choices and ranges as the bridge sends with the opt-in on.
if(optIn){
 const d=dom();
 const ctx=vm.createContext({document:d.document,navigator:{},location:{search:'?sample&experimental',protocol:'file:'},URLSearchParams,window:{}});
 vm.runInContext(fs.readFileSync('tools/launcher/ui/app.js','utf8'),ctx);
 const sampleState=JSON.parse(vm.runInContext('JSON.stringify(state)',ctx));assert(sampleState&&sampleState.settings.length,'the sample renders without a host');
 const real=new Map(optIn.settings.map(s=>[s.id,s]));
 assert.deepStrictEqual(sampleState.settings.map(s=>s.id).sort(),[...real.keys()].sort(),'sample settings differ from the table');
 for(const s of sampleState.settings){const r=real.get(s.id);
  assert.strictEqual(s.title,r.title,s.id+' sample title');assert.strictEqual(s.category,r.category,s.id+' sample page');
  assert.strictEqual(s.description,r.description,s.id+' sample description');
  assert.deepStrictEqual(s.choices.map(c=>c.label),r.choices.map(c=>c.label),s.id+' sample choices');
  assert.deepStrictEqual(s.choices.map(c=>/experimental|awaiting/.test(c.maturity)),r.choices.map(c=>/experimental|awaiting/.test(c.maturity)),s.id+' sample maturity');
  if(!r.choices.length)assert.deepStrictEqual([s.min,s.max,s.step,s.unit,!!s.hasOff],[r.min,r.max,r.step,r.unit,r.hasOff],s.id+' sample range');
 }
 assert.deepStrictEqual(Object.keys(sampleState.shortcuts),Object.keys(optIn.shortcuts),'sample hotkeys');
 assert.deepStrictEqual(sampleState.presetDescriptions,optIn.presetDescriptions,'sample preset descriptions');
}
console.log('Renderer: shell, Home, preparation, rows and groups, detail panel, restart badges, Hotkeys, Help, Experimental page, sample data and developer authorization pass.');
