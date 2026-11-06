// Built-in file manager page served at /_manager (stored in flash).
// The page is plain HTML/CSS/JS inside the raw string below; it talks to the board with normal WebDAV requests.
#pragma once
#include <pgmspace.h>

static const char MANAGER_HTML[] PROGMEM = R"MGRHTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SD Card Files</title>
<style>
:root{--bg:#f6f7f9;--fg:#1d2330;--mut:#6b7385;--card:#fff;--line:#e3e6ec;--acc:#2563eb;--err:#b42318;--ok:#067647;--hov:#f0f3f8}
@media (prefers-color-scheme:dark){:root{--bg:#14171c;--fg:#e6e9ef;--mut:#9aa3b2;--card:#1c2027;--line:#2c323c;--acc:#6ea8fe;--err:#ff8a80;--ok:#6fdc9a;--hov:#232831}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 16px;border-bottom:1px solid var(--line);background:var(--card);display:flex;flex-wrap:wrap;gap:8px;align-items:center}
h1{font-size:17px;margin:0 12px 0 0}#crumbs a{color:var(--acc);text-decoration:none;cursor:pointer}#crumbs span{color:var(--mut);margin:0 4px}
main{max-width:1000px;margin:0 auto;padding:16px}
.bar{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:12px}
button,.btn{font:inherit;padding:6px 12px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);cursor:pointer}
button:hover,.btn:hover{background:var(--hov)}button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.del{color:var(--err)}
button:disabled{opacity:.5;cursor:default}
#msg{min-height:22px;margin-bottom:8px;color:var(--ok)}#msg.err{color:var(--err)}
table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line);border-radius:8px;overflow:hidden}
th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);vertical-align:middle}
th{font-size:13px;color:var(--mut);font-weight:600}tr:last-child td{border-bottom:0}tbody tr:hover{background:var(--hov)}
td.n a{color:var(--fg);text-decoration:none;cursor:pointer;overflow-wrap:anywhere}td.n a:hover{color:var(--acc)}
td.s,td.m{color:var(--mut);white-space:nowrap;font-size:13px}td.a{white-space:nowrap;text-align:right}td.a button{padding:3px 8px;font-size:13px}
.ic{display:inline-block;width:1.4em}
#drop{display:none;position:fixed;inset:0;background:rgba(37,99,235,.15);border:3px dashed var(--acc);z-index:5;align-items:center;justify-content:center;font-size:22px;color:var(--acc)}
#prog{display:none;margin-bottom:10px}#prog div{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:4px}#prog i{display:block;height:100%;width:0;background:var(--acc)}
dialog{border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--fg);width:min(460px,92vw);padding:16px}
dialog::backdrop{background:rgba(0,0,0,.4)}#pl{max-height:300px;overflow:auto;border:1px solid var(--line);border-radius:6px;margin:10px 0}
#pl div{padding:7px 10px;cursor:pointer;border-bottom:1px solid var(--line)}#pl div:hover{background:var(--hov)}
#card{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;padding:8px 12px;margin-bottom:10px;border:1px solid var(--line);border-radius:8px;background:var(--card);font-size:14px}#card b{font-weight:600}#cb{margin-left:auto}#card.pr{border-color:var(--acc)}
.empty{color:var(--mut);text-align:center;padding:24px}
@media (max-width:640px){td.m,th.m{display:none}td.a button{padding:3px 6px}}
</style></head><body>
<header><h1 id="host">SD Card</h1><nav id="crumbs"></nav></header>
<main>
<div class="bar">
<button id="up">&#8593; Up</button>
<button id="mk">New folder</button>
<label class="btn pri" style="background:var(--acc);color:#fff;border-color:var(--acc)">Upload files<input id="file" type="file" multiple hidden></label>
<button id="rf">Refresh</button>
<label class="btn" style="display:inline-flex;align-items:center;gap:6px"><input type="checkbox" id="hid" style="margin:0">Show hidden files</label>
<label class="btn" id="fwb" style="display:none" title="Install a firmware .bin made with Sketch &gt; Export Compiled Binary, or the same file gzipped (.bin.gz)">Update firmware<input id="fw" type="file" accept=".bin,.gz" hidden></label>
</div>
<div id="prog"><span id="pt"></span><div><i id="pb"></i></div></div>
<div id="card"><span id="cs"></span><button id="cb"></button></div>
<div id="msg"></div>
<table><thead><tr><th>Name</th><th>Size</th><th class="m">Modified</th><th></th></tr></thead><tbody id="tb"></tbody></table>
<p style="color:var(--mut);font-size:13px">Drag files onto this page to upload them to the current folder.<span id="cert"></span><br><span id="fwv"></span></p>
</main>
<div id="drop">Drop files to upload</div>
<dialog id="dlg"><b id="dt">Move to</b><div style="margin-top:6px;color:var(--mut);font-size:13px" id="dp"></div><div id="pl"></div>
<div class="bar" style="justify-content:flex-end;margin:0"><button id="dc">Cancel</button><button class="pri" id="dok">Move here</button></div></dialog>
<script>
const $=s=>document.querySelector(s);
let cur='/',busyOp=false;
// names starting with '.' (e.g. macOS ._ files) are hidden unless the box is ticked; the choice is remembered
let showHidden=false;try{showHidden=localStorage.getItem('showHidden')==='1'}catch(e){}
const vis=a=>showHidden?a:a.filter(i=>!i.name.startsWith('.'));
if(location.protocol==='https:')$('#cert').innerHTML=' &middot; <a href="/_cert" style="color:var(--acc)">Download this board\'s certificate</a> (install it as trusted to stop the browser warning)';
const enc=p=>p.split('/').map(encodeURIComponent).join('/');
const join=(a,b)=>(a.endsWith('/')?a:a+'/')+b;
const parent=p=>{p=p.replace(/\/+$/,'');const i=p.lastIndexOf('/');return i<=0?'/':p.slice(0,i)};
const bad=n=>!n||/[\\\/:*?"<>|]/.test(n)||n==='.'||n==='..';
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function msg(t,err){const m=$('#msg');m.textContent=t||'';m.className=err?'err':''}
function size(n){if(n<1024)return n+' B';const u=['KB','MB','GB'];let i=-1;do{n/=1024;i++}while(n>=1024&&i<2);return n.toFixed(n<10?1:0)+' '+u[i]}
function busyErr(b){setTimeout(()=>cardShow(),0);return new Error(/Printer mode/.test(b||'')?'The card is in Printer mode, so this page can\'t use it. Click "Use card from here" to browse it.':'The printer is using the SD card right now. Try again in a few seconds.'+(b?' ('+b+')':''))}
async function dav(method,path,opts){const r=await fetch(enc(path),Object.assign({method:method},opts||{}));const b=r.headers.get('X-Busy');if(b)throw busyErr(b);return r}
async function list(path){
 const r=await dav('PROPFIND',path,{headers:{Depth:'1'}});
 if(!r.ok)throw new Error('Could not list '+path+' (HTTP '+r.status+')');
 const x=new DOMParser().parseFromString(await r.text(),'application/xml');
 const rs=x.getElementsByTagNameNS('DAV:','response'),out=[];
 for(let i=1;i<rs.length;i++){
  const g=t=>{const e=rs[i].getElementsByTagNameNS('DAV:',t)[0];return e?e.textContent:''};
  let name=g('href').replace(/\/+$/,'').split('/').pop();try{name=decodeURIComponent(name)}catch(e){}
  if(!name)continue;
  out.push({name:name,dir:rs[i].getElementsByTagNameNS('DAV:','collection').length>0,size:+g('getcontentlength')||0,mod:g('getlastmodified')});
 }
 out.sort((a,b)=>(b.dir-a.dir)||a.name.localeCompare(b.name,undefined,{numeric:true,sensitivity:'base'}));
 return out;
}
function crumbs(){
 const c=$('#crumbs');c.innerHTML='';const parts=cur.split('/').filter(Boolean);
 const add=(t,p)=>{const a=document.createElement('a');a.textContent=t;a.onclick=()=>go(p);c.appendChild(a)};
 add('Root','/');let p='';
 for(const s of parts){p+='/'+s;const sp=document.createElement('span');sp.textContent='/';c.appendChild(sp);add(s,p)}
}
async function go(path){
 cur=path||'/';history.replaceState(null,'',cur==='/'?location.pathname:'#'+enc(cur));
 crumbs();$('#up').disabled=cur==='/';
 const tb=$('#tb');tb.innerHTML='<tr><td colspan="4" class="empty">Loading&hellip;</td></tr>';
 try{
  const all=await list(cur),items=vis(all);tb.innerHTML='';
  if(!items.length)tb.innerHTML='<tr><td colspan="4" class="empty">'+(all.length?'Only hidden files here (tick Show hidden files)':'This folder is empty')+'</td></tr>';
  for(const it of items){
   const p=join(cur,it.name),tr=document.createElement('tr');
   const d=it.mod?new Date(it.mod):null;
   tr.innerHTML='<td class="n"><span class="ic">'+(it.dir?'&#128193;':'&#128196;')+'</span><a></a></td><td class="s">'+(it.dir?'':size(it.size))+'</td><td class="m">'+(d&&!isNaN(d)?d.toLocaleString():'')+'</td><td class="a"><button>Rename</button> <button>Move</button> <button class="del">Delete</button></td>';
   const a=tr.querySelector('a');a.textContent=it.name;
   if(it.dir)a.onclick=()=>go(p);else{a.href=enc(p);a.setAttribute('download',it.name)}
   const b=tr.querySelectorAll('button');
   b[0].onclick=()=>rename(p,it);b[1].onclick=()=>move(p,it);b[2].onclick=()=>del(p,it);
   tb.appendChild(tr);
  }
 }catch(e){tb.innerHTML='<tr><td colspan="4" class="empty">Could not load this folder</td></tr>';msg(e.message,true)}
}
async function act(label,fn){
 if(busyOp)return;busyOp=true;msg(label+'…');
 try{await fn();await go(cur)}catch(e){msg(e.message,true)}finally{busyOp=false}
}
async function moveReq(from,to){
 const r=await dav('MOVE',from,{headers:{Destination:location.origin+enc(to),Overwrite:'F'}});
 if(!r.ok)throw new Error('Could not move/rename (HTTP '+r.status+'). Does "'+to+'" already exist?');
}
function rename(p,it){
 const n=prompt('New name for "'+it.name+'":',it.name);if(n===null||n===it.name)return;
 if(bad(n))return msg('That name has characters that are not allowed: \\ / : * ? " < > |',true);
 act('Renaming',async()=>{await moveReq(p,join(parent(p),n));msg('Renamed to '+n)});
}
function del(p,it){
 if(!confirm(it.dir?'Delete the folder "'+it.name+'" and everything inside it?':'Delete "'+it.name+'"?'))return;
 act('Deleting',async()=>{const r=await dav('DELETE',p);if(!r.ok)throw new Error('Could not delete "'+it.name+'" (HTTP '+r.status+')');msg('Deleted '+it.name)});
}
$('#mk').onclick=()=>{
 const n=prompt('Name of the new folder:');if(!n)return;
 if(bad(n))return msg('That name has characters that are not allowed: \\ / : * ? " < > |',true);
 act('Creating folder',async()=>{const r=await dav('MKCOL',join(cur,n));if(!r.ok)throw new Error('Could not create "'+n+'" (HTTP '+r.status+'). Does it already exist?');msg('Created folder '+n)});
};
// folder picker for Move
let pick='/',moving=null;
async function pickGo(p){
 pick=p;$('#dp').textContent='Destination: '+p;const pl=$('#pl');pl.innerHTML='<div>Loading&hellip;</div>';
 try{
  const dirs=vis(await list(p)).filter(i=>i.dir);pl.innerHTML='';
  if(p!=='/'){const u=document.createElement('div');u.innerHTML='&#8593; Up';u.onclick=()=>pickGo(parent(p));pl.appendChild(u)}
  for(const d of dirs){const q=join(p,d.name);if(moving&&q===moving.p)continue;const e=document.createElement('div');e.innerHTML='&#128193; '+esc(d.name);e.onclick=()=>pickGo(q);pl.appendChild(e)}
  if(!pl.children.length)pl.innerHTML='<div style="cursor:default;color:var(--mut)">No folders here</div>';
 }catch(e){pl.innerHTML='<div style="color:var(--err)">'+esc(e.message)+'</div>'}
}
function move(p,it){moving={p:p,it:it};$('#dt').textContent='Move "'+it.name+'" to';$('#dlg').showModal();pickGo(parent(p))}
$('#dc').onclick=()=>$('#dlg').close();
$('#dok').onclick=()=>{
 const m=moving,to=join(pick,m.it.name);$('#dlg').close();
 if(to===m.p)return;
 if(m.it.dir&&(pick+'/').startsWith(m.p+'/'))return msg('A folder cannot be moved inside itself',true);
 act('Moving',async()=>{await moveReq(m.p,to);msg('Moved '+m.it.name+' to '+pick)});
};
// uploads (one at a time, the board handles one request at a time)
function put(path,file){
 return new Promise((ok,no)=>{
  const x=new XMLHttpRequest();x.open('PUT',enc(path));
  x.upload.onprogress=e=>{if(e.lengthComputable)$('#pb').style.width=(100*e.loaded/e.total)+'%'};
  x.onload=()=>{const b=x.getResponseHeader('X-Busy');if(b)return no(busyErr(b));(x.status>=200&&x.status<300)?ok():no(new Error('Upload of "'+file.name+'" failed (HTTP '+x.status+')'))};
  x.onerror=()=>no(new Error('Upload of "'+file.name+'" failed (connection lost)'));
  x.send(file);
 });
}
async function upload(files){
 files=[...files];if(!files.length||busyOp)return;busyOp=true;msg('');$('#prog').style.display='block';
 let done=0;
 try{
  for(const f of files){
   if(bad(f.name))throw new Error('"'+f.name+'" has characters that are not allowed in a file name');
   $('#pt').textContent='Uploading '+f.name+' ('+(done+1)+' of '+files.length+', '+size(f.size)+')';$('#pb').style.width='0';
   await put(join(cur,f.name),f);done++;
  }
  msg('Uploaded '+done+' file'+(done==1?'':'s'));
 }catch(e){msg((done?done+' uploaded, then: ':'')+e.message,true)}
 finally{busyOp=false;$('#prog').style.display='none';$('#file').value='';go(cur)}
}
$('#file').onchange=e=>upload(e.target.files);
let dragN=0;
addEventListener('dragenter',e=>{if([...e.dataTransfer.types].includes('Files')){dragN++;$('#drop').style.display='flex'}});
addEventListener('dragleave',()=>{if(--dragN<=0){dragN=0;$('#drop').style.display='none'}});
addEventListener('dragover',e=>e.preventDefault());
addEventListener('drop',e=>{e.preventDefault();dragN=0;$('#drop').style.display='none';upload(e.dataTransfer.files)});
$('#hid').checked=showHidden;
$('#hid').onchange=e=>{showHidden=e.target.checked;try{localStorage.setItem('showHidden',showHidden?'1':'0')}catch(x){}go(cur)};
// firmware update: the .bin goes to /_update, the board installs it and restarts
$('#fw').onchange=async e=>{
 let f=e.target.files[0];$('#fw').value='';if(!f||busyOp)return;
 if(!/\.(bin|gz)$/i.test(f.name))return msg('Pick the .bin file made with Sketch > Export Compiled Binary (or a gzipped .bin.gz)',true);
 if(!confirm('Install firmware "'+f.name+'" ('+size(f.size)+')?\n\nKeep the board powered while it installs. It restarts afterwards; the old firmware is kept if anything goes wrong.'))return;
 busyOp=true;msg('');$('#prog').style.display='block';$('#pb').style.width='0';
 // an uncompressed .bin is gzipped here first: it is much smaller, and the board unpacks it when it restarts
 let note='';
 try{
  const head=new Uint8Array(await f.slice(0,1).arrayBuffer());
  if(head[0]===0xE9&&window.CompressionStream){
   $('#pt').textContent='Compressing '+f.name+'…';
   const gz=await new Response(f.stream().pipeThrough(new CompressionStream('gzip'))).blob();
   note=' (compressed '+size(f.size)+' to '+size(gz.size)+')';f=gz;
  }
 }catch(err){}
 $('#pt').textContent='Uploading firmware'+note;
 const x=new XMLHttpRequest();x.open('PUT','/_update');
 x.upload.onprogress=e=>{if(e.lengthComputable)$('#pb').style.width=(100*e.loaded/e.total)+'%'};
 const done=(ok,t)=>{busyOp=false;$('#prog').style.display='none';msg(t,!ok)};
 x.onload=()=>{if(x.status==200){done(true,'Firmware installed. The board is restarting; this page reloads in 30 seconds.');setTimeout(()=>location.reload(),30000)}
  else done(false,(x.responseText||'Firmware update failed (HTTP '+x.status+')').trim())};
 x.onerror=()=>done(false,'Firmware upload failed (connection lost). The old firmware is still running.');
 x.send(f);
};
// the board always offers network updates (with AUTH=1 the login applies)
fetch('/_info').then(r=>{if(r.headers.get('X-Update')==='1')$('#fwb').style.display='';return r.ok?r.text():''}).then(t=>{$('#fwv').textContent=t}).catch(()=>{});
$('#up').onclick=()=>go(parent(cur));$('#rf').onclick=()=>go(cur);
$('#host').textContent=location.hostname+' SD card';document.title=location.hostname+' files';
// card owner switch: Network = this page and WebDAV may use the card; Printer = the board leaves it alone
let cardMode='';
async function cardShow(st){
 if(!st){try{const r=await fetch('/_card');if(!r.ok)return;st=await r.text()}catch(e){return}}
 st=st.trim();const prev=cardMode;cardMode=st;
 const c=$('#card'),b=$('#cb');c.className=st==='printer'?'pr':'';
 if(st==='printer'){$('#cs').innerHTML='<b>SD card: Printer mode.</b> The board leaves the card to the printer (use Init media on the printer if it says no media).';b.textContent='Use card from here';b.className='pri'}
 else{$('#cs').innerHTML='<b>SD card: Network mode.</b> '+(st==='busy'?'The printer is using the card right now; this page waits for it.':'Shared: the board uses it when the printer isn\'t.');b.textContent='Give card to printer';b.className=''}
 if(prev==='printer'&&st!=='printer')go(cur);
}
$('#cb').onclick=async()=>{
 const to=cardMode==='printer'?'network':'printer';
 if(to==='printer'&&busyOp)return msg('Wait for the current upload to finish first',true);
 try{const r=await fetch('/_card?'+to,{method:'POST'});await cardShow(await r.text());
  msg(to==='printer'?'Card handed to the printer. On the printer, use Init media (or M21) if it shows no media.':'Card is shared again.');
  if(to==='printer'){$('#tb').innerHTML='<tr><td colspan="4" class="empty">The card is in Printer mode</td></tr>'}}
 catch(e){msg('Could not switch: '+e.message,true)}
};
cardShow();setInterval(()=>{if(!document.hidden)cardShow()},10000);
go(location.hash.length>1?decodeURIComponent(location.hash.slice(1)):'/');
</script></body></html>)MGRHTML";
