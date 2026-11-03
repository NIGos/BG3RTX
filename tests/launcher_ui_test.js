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
for(const category of new Set(fixture.settings.map(s=>s.category))){
 assert(ids.has('nav-'+category),category+' has no page');ids.get('nav-'+category).onclick();
 const cards=all(ids.get('section-content')).filter(e=>e.tagName==='article'&&e.id);
 for(const s of fixture.settings.filter(s=>s.category===category)){
  assert(cards.some(c=>c.id==='card-'+s.id),s.id+' unreachable');
  const card=cards.find(c=>c.id==='card-'+s.id);
  assert(all(card).some(e=>e.textContent.includes('GPU cost:')),s.id+' missing cost');
  for(const control of card.querySelectorAll('button,input'))assert(control.id&&control.attributes['aria-describedby']);
  const reasons=ids.get('needs-'+s.id).textContent;
  for(const ch of s.choices){if(ch.reason)assert(reasons.includes(ch.reason));if(ch.requirements)assert(reasons.includes(ch.requirements));}
  for(const e of card.querySelectorAll('button,input'))assert(ids.has(e.attributes['aria-describedby']));
 }
}
ids.get('nav-Lighting').onclick();
const rr=ids.get('control-rr-0');assert(rr.type==='checkbox'&&rr.attributes['aria-label']);
rr.focus();render(fixture);assert.strictEqual(document.activeElement,ids.get(rr.id),'focus lost on bridge update');
const checkbox=ids.get(rr.id);let clicked=false;checkbox.click=()=>clicked=true;checkbox.onkeydown({key:'Enter',preventDefault(){}});assert(clicked);
for(let p=0;p<4;p++){ids.get('preset-'+p).onclick();assert.deepStrictEqual(JSON.parse(JSON.stringify(sent.at(-1))),{type:'preset',value:p});}
const advanced=fixture.settings.filter(s=>s.experimental);assert(advanced.length);
ids.get('nav-Advanced').onclick();const summary=ids.get('experimental-summary');assert.strictEqual(summary.tagName,'summary');
// Explicit bridge authorization, rather than the presence of a developer row.
const dev={...fixture.settings[0],id:'test-dev',developer:true};
render({...fixture,settings:[...fixture.settings,dev]});assert(!ids.get('navigation').children.some(e=>e.id==='nav-Developer'));
render({...fixture,developer:true,settings:[...fixture.settings,dev]});ids.get('nav-Developer').onclick();assert(all(ids.get('section-content')).some(e=>e.id==='card-test-dev'));
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
const reportState={...fixture,report,status:'Package preview ready'};render(reportState);
const reportSummary=ids.get('report-summary-graphics.txt'),fileDetails=ids.get('report-file-graphics.txt');fileDetails.open=true;reportSummary.focus();
render(reportState);assert.strictEqual(ids.get('report-summary-graphics.txt'),reportSummary,'unchanged report rebuilt');assert.strictEqual(document.activeElement,reportSummary);assert(fileDetails.open);
render({...reportState,report:{...report,files:[{...report.files[0],text:'Updated sanitized text'}]}});
assert.notStrictEqual(ids.get('report-summary-graphics.txt'),reportSummary);assert(ids.get('report-file-graphics.txt').open,'updated report disclosure collapsed');assert.strictEqual(document.activeElement,ids.get('report-summary-graphics.txt'),'updated report lost dialog focus');
console.log('Renderer: category reachability, requirements, costs, focus, native controls, presets and developer authorization pass.');
