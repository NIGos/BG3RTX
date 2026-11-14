// Offline launcher page review in headless Edge, with the sample data of ui/app.js (no game, no launcher exe):
//   node tools/launcher/ui_shots.js <out dir> [--sizes 1280x800,1000x700,640x700] [--only name,name] [--keep name,name]
// Every page and state is rendered from the shipped index.html at every size. For each one it prints the layout
// problems it finds (a page wider than the window, clipped boxes, text outside its box, controls outside the window
// or on each other) and writes <name>-<w>x<h>.png. --keep limits the PNGs to those names (all sizes are still audited).
// One Edge process, driven over the DevTools protocol (Node 22+ has WebSocket built in); a temporary profile.
'use strict';
const fs=require('fs'),os=require('os'),path=require('path'),{spawn}=require('child_process');
const root=path.resolve(__dirname,'../..'),index='file:///'+path.join(root,'tools/launcher/ui/index.html').replace(/\\/g,'/').replace(/ /g,'%20');
const views={
 'home':'section=Home','home-missing':'view=missing','home-granny':'section=Home&view=granny','home-hair-stopped':'section=Home&view=hairerror',
 'lighting':'section=Lighting','lighting-hair':'section=Lighting&select=exp_hair_groom_primary','lighting-hair-detail':'section=Lighting&select=hair_distant_detail',
 'lighting-notice':'view=notice','lighting-restart':'section=Lighting&view=restart',
 'surfaces':'section=Surfaces&select=exp_water','performance':'section=Performance&select=scene_handout','performance-pipeline':'section=Performance&select=exp_gi_rt_pipeline_reorder',
 'dlss5':'section=DLSS%205','experimental-off':'section=Experimental','experimental-on':'section=Experimental&experimental=1&select=exp_hair_strand_aa',
 'hotkeys':'section=Hotkeys','support':'section=Support','support-probe':'view=probe','write-error':'section=Lighting&view=error','report':'section=Support&report=1'};
// Runs in the page. Returns the list of problems. 1 px clips are the screen-reader-only texts; a control counts for
// the overlap check only when it is on screen (not scrolled under another box).
const audit=`(()=>{const out=[],W=innerWidth,seen=new Set();
 const name=e=>(e.id?'#'+e.id:e.tagName.toLowerCase()+(typeof e.className==='string'&&e.className.trim()?'.'+e.className.trim().split(/\\s+/).join('.'):''));
 const where=e=>{const p=[];for(let x=e;x&&x!==document.body&&p.length<3;x=x.parentElement)p.unshift(name(x));return p.join(' > ');};
 const add=(kind,e,extra)=>{const k=kind+where(e);if(seen.has(k))return;seen.add(k);out.push(kind+': '+where(e)+(extra?' '+extra:'')+' "'+(e.textContent||'').trim().slice(0,50)+'"');};
 const skip=e=>e.closest('dialog:not([open])')||e.closest('[hidden]')||e.closest('svg')||e.closest('details:not([open]) > :not(summary)')||e.closest('.toggle')||e.closest('.visually-hidden')||e.closest('#titlebar')||e.closest('footer.idle')||(document.querySelector('dialog[open]')&&!e.closest('dialog[open]'));
 const field=e=>/^(INPUT|SELECT|TEXTAREA|PROGRESS)$/.test(e.tagName);
 if(document.documentElement.scrollWidth>W+1)out.push('page wider than the window: '+document.documentElement.scrollWidth+' > '+W);
 document.querySelectorAll('body *').forEach(e=>{
  const r=e.getBoundingClientRect(),cs=getComputedStyle(e);if(!r.width||!r.height||cs.visibility==='hidden'||skip(e)||String(cs.clipPath).includes('inset(50%)'))return;
  const clipsX=cs.overflowX!=='visible',clipsY=cs.overflowY!=='visible',scrolls=/auto|scroll/.test(cs.overflowX+cs.overflowY);
  if(e.scrollWidth>e.clientWidth+1&&clipsX&&!scrolls&&e.clientWidth>0&&!field(e))add('clipped (x)',e,e.scrollWidth+'>'+e.clientWidth);
  if(e.scrollHeight>e.clientHeight+2&&clipsY&&!scrolls&&e.clientHeight>0&&!field(e))add('clipped (y)',e,e.scrollHeight+'>'+e.clientHeight);
  if(/^(BUTTON|INPUT|SELECT|A)$/.test(e.tagName)&&(r.right>W+1||r.left<-1))add('control outside the window',e,Math.round(r.left)+'..'+Math.round(r.right));
  const p=e.parentElement;if(p&&p!==document.body){const pr=p.getBoundingClientRect(),ps=getComputedStyle(p);
   if(ps.overflowX==='visible'&&r.right>pr.right+2&&cs.position!=='absolute'&&cs.position!=='fixed'&&cs.display!=='inline')add('outside its box',e,Math.round(r.right)+'>'+Math.round(pr.right));}
 });
 const bs=[...document.querySelectorAll('button,input,select')].filter(b=>{const r=b.getBoundingClientRect();if(!r.width||!r.height||skip(b))return false;const top=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);return !!top&&(b.contains(top)||top.contains(b));}).map(b=>[b,b.getBoundingClientRect()]);
 for(let i=0;i<bs.length;i++)for(let j=i+1;j<bs.length;j++){const a=bs[i][1],b=bs[j][1];
  const ox=Math.min(a.right,b.right)-Math.max(a.left,b.left),oy=Math.min(a.bottom,b.bottom)-Math.max(a.top,b.top);if(ox>2&&oy>2)add('controls overlap',bs[i][0],'with '+where(bs[j][0]));}
 return out;})()`;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function main() {
 const args=process.argv.slice(2),opt=k=>{const i=args.indexOf(k);return i<0?'':args[i+1]||'';};
 const out=args[0];if(!out||out.startsWith('--')){console.error('usage: node tools/launcher/ui_shots.js <out dir> [--sizes WxH,...] [--only names] [--keep names]');process.exit(2);}
 const sizes=(opt('--sizes')||'1280x800,1000x700,640x700').split(',').map(s=>s.split('x').map(Number));
 const only=opt('--only').split(',').filter(Boolean),keep=opt('--keep').split(',').filter(Boolean);
 const names=Object.keys(views).filter(n=>!only.length||only.includes(n));
 const edge=['C:\\Program Files (x86)','C:\\Program Files'].map(p=>p+'\\Microsoft\\Edge\\Application\\msedge.exe').find(p=>fs.existsSync(p));
 if(!edge){console.error('Microsoft Edge not found');process.exit(2);}
 fs.mkdirSync(out,{recursive:true});
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'bg3rtx-ui-shots-')),port=9300+Math.floor(Math.random()*400);
 const child=spawn(edge,['--headless=new','--disable-gpu','--hide-scrollbars','--no-first-run','--allow-file-access-from-files','--remote-debugging-port='+port,'--user-data-dir='+profile,'about:blank'],{stdio:'ignore'});
 try{os.setPriority(child.pid,os.constants.priority.PRIORITY_BELOW_NORMAL);}catch(e){}
 let target;
 for(let i=0;i<100&&!target;i++){await sleep(200);try{target=(await (await fetch('http://127.0.0.1:'+port+'/json')).json()).find(t=>t.type==='page');}catch(e){}}
 if(!target){console.error('Edge did not open its DevTools port');child.kill();process.exit(2);}
 const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,bad)=>{ws.onopen=ok;ws.onerror=bad;});
 let id=0;const waiting=new Map(),events=[];
 ws.onmessage=m=>{const d=JSON.parse(m.data);if(d.id&&waiting.has(d.id)){waiting.get(d.id)(d);waiting.delete(d.id);}else if(d.method)events.push(d.method);};
 const cdp=(method,params={})=>new Promise((ok,bad)=>{const n=++id;waiting.set(n,d=>d.error?bad(new Error(method+': '+d.error.message)):ok(d.result));ws.send(JSON.stringify({id:n,method,params}));});
 await cdp('Page.enable');
 let done=0,problems=0;const total=names.length*sizes.length;
 for(const name of names)for(const [w,h] of sizes){
  await cdp('Emulation.setDeviceMetricsOverride',{width:w,height:h,deviceScaleFactor:1,mobile:false});
  events.length=0;await cdp('Page.navigate',{url:index+'?sample=1&chrome=1&'+views[name]});
  for(let i=0;i<50&&!events.includes('Page.loadEventFired');i++)await sleep(100);
  await cdp('Runtime.evaluate',{expression:'document.fonts.ready.then(()=>1)',awaitPromise:true});await sleep(150);
  const found=(await cdp('Runtime.evaluate',{expression:audit,returnByValue:true})).result.value||['audit did not run'];
  const file=name+'-'+w+'x'+h+'.png';
  if(!keep.length||keep.includes(name))fs.writeFileSync(path.join(out,file),Buffer.from((await cdp('Page.captureScreenshot',{format:'png'})).data,'base64'));
  done++;problems+=found.length;console.log('['+done+'/'+total+'] '+file+': '+found.length+' problem(s)');found.forEach(f=>console.log('    '+f));
 }
 ws.close();child.kill();await sleep(300);
 try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200});}catch(e){}
 console.log(done+' pages, '+problems+' problem(s)');
}
main().catch(e=>{console.error(e);process.exit(1);});
