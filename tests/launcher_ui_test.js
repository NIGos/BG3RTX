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
 querySelectorAll(selector){const tags=selector.split(',');return this.children.flatMap(c=>typeof c==='object'?[...(tags.includes(c.tagName)?[c]:[]),...c.querySelectorAll(selector)]:[]);}
}
const document={head:new Element('head'),documentElement:new Element('html'),activeElement:null,
 createElement:t=>new Element(t),createTextNode:t=>t,getElementById:id=>ids.get(id),querySelector:()=>new Element('img')};
for(const match of fs.readFileSync('tools/launcher/ui/index.html','utf8').matchAll(/id="([^"]+)"/g)){const e=new Element('div');e.id=match[1];}
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
console.log('Renderer: category reachability, requirements, costs, focus, native controls, presets and developer authorization pass.');
