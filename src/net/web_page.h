#pragma once
#include <pgmspace.h>

namespace net {

// Single page served at "/": one section at a time behind a tab bar (projects, samples, MIDI,
// wavetables, presets, diagnostics, firmware; the tab is kept in the URL hash), file lists scrolling in their own
// box with a name filter, upload (drag & drop), rename, delete, firmware update;
// samples, wavetables and presets also have subfolders (breadcrumb, new folder, delete an empty folder;
// presets sit in type folders FM / DRUM / SAMPLE / CHIP / SYNTH); projects have one level of project
// folders with their samples and a "wt" folder of wavetables (made and removed by the tracker, no new
// folder button).
// No external resources: works without internet access.
static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>D-TRK</title>
<style>
:root{--bg:#111;--fg:#eee;--dim:#888;--acc:#fe0;--box:#1d1d1d;--line:#2a2a2a;--err:#f55}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line)}
.bar{max-width:820px;margin:0 auto;padding:12px 16px 0}
h1{font-size:18px;margin:0 0 2px}header p{color:var(--dim);margin:0 0 8px;font-size:13px}
nav{display:flex;gap:2px;overflow-x:auto;scrollbar-width:none}nav::-webkit-scrollbar{display:none}
nav button{margin:0;border:0;border-bottom:2px solid transparent;border-radius:0;background:none;color:var(--dim);
  padding:8px 12px;white-space:nowrap;font:inherit}
nav button.on{color:var(--fg);border-bottom-color:var(--acc)}nav button:hover{color:var(--fg)}
main{max-width:820px;margin:0 auto;padding:12px 16px 16px}
section{display:none}section.on{display:block}
h2{display:none}
p.note{color:var(--dim);margin:0 0 8px;font-size:13px}
.tools{display:flex;gap:8px;align-items:center;margin-top:8px;flex-wrap:wrap}
.tools input{flex:1;min-width:140px;background:var(--box);color:var(--fg);border:1px solid #444;border-radius:6px;padding:5px 8px;font:inherit}
.tools .cnt{color:var(--dim);font-size:13px;white-space:nowrap}
.list{margin-top:8px;max-height:calc(100vh - 330px);min-height:180px;overflow:auto;border:1px solid var(--line);border-radius:8px}
.drop{border:2px dashed #444;border-radius:8px;padding:10px;text-align:center;color:var(--dim);cursor:pointer}
.drop.over{border-color:var(--acc);color:var(--fg)}
table{width:100%;border-collapse:collapse}
td{padding:6px 8px;border-bottom:1px solid #222;word-break:break-all}
tr.hide{display:none}
td.sz{color:var(--dim);white-space:nowrap;text-align:right;width:70px}
td.act{white-space:nowrap;text-align:right;width:1%}
a{color:var(--fg)}
button{background:var(--box);color:var(--fg);border:1px solid #444;border-radius:6px;padding:4px 10px;margin-left:4px;cursor:pointer}
button:hover{border-color:var(--acc)}
.st{min-height:1.4em;margin-top:8px;color:var(--dim)}.st.err{color:var(--err)}
progress{width:100%;display:none;margin-top:8px}
.crumb{margin:8px 0 0;display:flex;align-items:center;gap:4px;flex-wrap:wrap}.crumb a{color:var(--acc);cursor:pointer}
.crumb button{margin-left:auto}td.dir a{color:var(--acc);cursor:pointer}
</style></head><body>
<header><div class="bar">
<h1>D-TRK</h1>
<p>Files on the microSD card. While this page is open the tracker is in Wi-Fi mode.</p>
<nav><button data-t="projects">Projects</button><button data-t="samples">Samples</button><button data-t="midi">MIDI</button><button data-t="wavetables">Wavetables</button><button data-t="presets">Presets</button><button data-t="diag">Diag</button><button data-t="fw">Firmware</button></nav>
</div></header>
<main>

<section id="midi"><h2>MIDI (/midi)</h2>
<p class="note">Only .mid, up to 512 KB. Import on the tracker: FILE &rarr; Import MIDI.</p>
<div class="drop">Drop files here or click to choose<input type="file" accept=".mid" multiple hidden></div>
<progress max="100"></progress><div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="projects"><h2>Projects (/projects)</h2>
<p class="note">.mtp and .bak, name up to 16 characters: A-Z, digits, - and _. The folder named after a project holds its samples (.wav up to 10 MB, name up to 16 characters), its wt subfolder the project's wavetables: the tracker writes them on save and loads them with the project. To move a project, copy the .mtp and its whole folder.</p>
<div class="crumb"><span></span></div>
<div class="drop">Drop files here or click to choose<input type="file" accept=".mtp,.bak" multiple hidden></div>
<progress max="100"></progress><div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="samples"><h2>Samples (/samples)</h2>
<p class="note">Only .wav, up to 4 MB. Import into the bank on the tracker: FILE &rarr; SAMPLES. Folders allowed (up to 4 levels).</p>
<div class="crumb"><span></span><button>New folder</button></div>
<div class="drop">Drop files here or click to choose<input type="file" accept=".wav" multiple hidden></div>
<progress max="100"></progress><div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="wavetables"><h2>Wavetables (/wavetables)</h2>
<p class="note">Only .wav (PCM 8/16/24 bit; frames of 2048 points as in Serum, of 256, or by the clm chunk), up to 3 MB. Import on the tracker: INST SYNTH &rarr; table &rarr; IMPORT. Folders allowed (up to 4 levels).</p>
<div class="crumb"><span></span><button>New folder</button></div>
<div class="drop">Drop files here or click to choose<input type="file" accept=".wav" multiple hidden></div>
<progress max="100"></progress><div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="presets"><h2>Presets (/presets)</h2>
<p class="note">Only .mti, up to 1 KB, name up to 16 characters: A-Z, digits, - and _. A preset sits in the folder of its type (FM, DRUM, SAMPLE, CHIP, SYNTH); inside, folders are allowed (up to 4 levels). On the tracker: INST &rarr; presets.</p>
<div class="crumb"><span></span><button>New folder</button></div>
<div class="drop">Drop files here or click to choose<input type="file" accept=".mti" multiple hidden></div>
<progress max="100"></progress><div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="diag"><h2>Diagnostics (/diag)</h2>
<p class="note">Logs written by the tracker: crashlog.txt (the restarts after a crash, with a backtrace) and cpuprof.txt (PROJ &rarr; SYS &rarr; CPU profile results). Download or delete only.</p>
<div class="st"></div>
<div class="tools"><input type="search" placeholder="Filter by name"><span class="cnt"></span></div>
<div class="list"><table></table></div></section>

<section id="fw"><h2>Firmware</h2>
<p class="note">The file .pio/build/wt32/firmware.bin. The tracker restarts after the update.</p>
<div class="drop">Drop firmware.bin here or click to choose<input type="file" accept=".bin" hidden></div>
<progress max="100"></progress><div class="st"></div></section>
</main>
<script>
const $=(s,e=document)=>e.querySelector(s);
const kb=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(n<10240?1:0)+' KB':(n/1048576).toFixed(1)+' MB';
function status(sec,msg,err){const s=$('.st',sec);s.textContent=msg;s.className='st'+(err?' err':'');}
function send(url,file,sec){return new Promise(res=>{
  const x=new XMLHttpRequest(),f=new FormData(),pg=$('progress',sec);
  f.append('file',file,file.name);x.open('POST',url);
  pg.style.display='block';pg.value=0;
  x.upload.onprogress=e=>{if(e.lengthComputable)pg.value=e.loaded*100/e.total;};
  x.onload=()=>{pg.style.display='none';res({code:x.status,text:x.responseText});};
  x.onerror=()=>{pg.style.display='none';res({code:0,text:'no connection to the tracker'});};
  x.send(f);});}
// Subfolder per section: samples / wavetables / presets "" or "a/b", projects "", a project folder or its wt.
const sub={midi:'',projects:'',samples:'',wavetables:'',presets:'',diag:''};
const dq=(dir,s=sub[dir])=>'dir='+dir+(s?'&sub='+encodeURIComponent(s):'');
// Projects top: base name of a project file, "" for anything else.
const pbase=n=>{const m=/^(.+)\.(mtp|bak)$/.exec(n);return m?m[1]:'';};
// Projects top: a project's .mtp, its sample folder (also one not made yet), then its .bak.
function projSort(files){
  const lc=s=>s.toLowerCase(),dirs=new Set(files.filter(f=>f.dir).map(f=>lc(f.name)));
  for(const f of files){const b=pbase(f.name);
    if(b&&!dirs.has(lc(b))){dirs.add(lc(b));files.push({name:b,dir:1,virt:1});}}
  const key=f=>f.dir?f.name:pbase(f.name)||f.name,rank=f=>f.dir?1:/\.mtp$/.test(f.name)?0:/\.bak$/.test(f.name)?2:3;
  return files.sort((a,b)=>lc(key(a)).localeCompare(lc(key(b)))||rank(a)-rank(b));
}
function go(dir,s){sub[dir]=s;
  if(dir==='projects')$('#projects input').accept=s?'.wav':'.mtp,.bak';
  list(dir);}
function crumb(dir){
  const c=$('.crumb span',$('#'+dir));if(!c)return;c.innerHTML='';
  const parts=sub[dir]?sub[dir].split('/'):[];
  const add=(txt,s,last)=>{const a=document.createElement(last?'span':'a');a.textContent=txt;
    if(!last)a.onclick=()=>go(dir,s);c.appendChild(a);};
  add('/'+dir,'',!parts.length);
  parts.forEach((p,i)=>{c.appendChild(document.createTextNode(' / '));add(p,parts.slice(0,i+1).join('/'),i===parts.length-1);});
}
async function mkdir(dir){
  const sec=$('#'+dir),n=prompt('Folder name (A-Z, digits, space, . _ -)');if(!n)return;
  const r=await fetch('/api/mkdir?'+dq(dir)+'&name='+encodeURIComponent(n),{method:'POST'});
  status(sec,r.ok?'Folder created: '+n:await r.text(),!r.ok);list(dir);
}
async function list(dir){
  const sec=$('#'+dir),t=$('table',sec);crumb(dir);
  let r;try{r=await fetch('/api/list?'+dq(dir));}catch(e){status(sec,'no connection to the tracker',1);return;}
  if(!r.ok){
    if(r.status===404&&sub[dir]){status(sec,await r.text(),1);go(dir,'');return;}  // folder gone: back to the top
    status(sec,await r.text(),1);return;}
  const top=dir==='projects'&&!sub[dir],raw=await r.json();
  const files=top?projSort(raw):raw.sort((a,b)=>(b.dir||0)-(a.dir||0)||a.name.localeCompare(b.name));t.innerHTML='';
  // Deleting the last file of a project takes its sample folder along.
  const lastOf=n=>{const b=pbase(n).toLowerCase();return files.filter(f=>!f.dir&&pbase(f.name).toLowerCase()===b).length===1&&
    files.some(f=>f.dir&&!f.virt&&f.name.toLowerCase()===b);};
  if(sub[dir]){
    const tr=document.createElement('tr');tr.innerHTML='<td class="dir"><a>..</a></td><td></td><td></td>';
    $('a',tr).onclick=()=>go(dir,sub[dir].split('/').slice(0,-1).join('/'));t.appendChild(tr);}
  $('.cnt',sec).textContent=files.filter(f=>!f.virt).length+' files';
  if(!files.length){t.insertAdjacentHTML('beforeend','<tr><td class="sz" style="text-align:left">empty</td></tr>');return;}
  for(const f of files){
    const tr=document.createElement('tr'),q=dq(dir)+'&name='+encodeURIComponent(f.name);
    if(f.dir){
      // Project folders: made and removed by the tracker; preset type folders: the tracker needs them.
      const own=dir!=='projects',fixed=dir==='presets'&&!sub[dir],del=own&&!fixed;
      tr.innerHTML='<td class="dir"><a></a></td><td class="sz">'+(own?'folder':sub[dir]?'wavetables':'samples')+'</td><td class="act">'+(del?'<button>Delete</button>':'')+'</td>';
      const a=$('a',tr);a.textContent=f.name+'/';a.onclick=()=>go(dir,(sub[dir]?sub[dir]+'/':'')+f.name);
      if(del)$('button',tr).onclick=async()=>{if(!confirm('Delete the empty folder '+f.name+'?'))return;
        const r=await fetch('/api/rmdir?'+q,{method:'POST'});
        status(sec,r.ok?'Folder deleted: '+f.name:await r.text(),!r.ok);list(dir);};
      t.appendChild(tr);continue;}
    const ro=dir==='diag';  // logs: no rename
    tr.innerHTML='<td><a></a></td><td class="sz"></td><td class="act">'+(ro?'':'<button>Rename</button>')+'<button>Delete</button></td>';
    const a=$('a',tr);a.textContent=f.name;a.href='/api/file?'+q;a.download=f.name;
    $('.sz',tr).textContent=kb(f.size);
    const bs=tr.querySelectorAll('button'),bRen=ro?{}:bs[0],bDel=bs[bs.length-1];
    bRen.onclick=async()=>{const to=prompt('New name',f.name);if(!to||to===f.name)return;
      const r=await fetch('/api/rename?'+dq(dir)+'&from='+encodeURIComponent(f.name)+'&to='+encodeURIComponent(to),{method:'POST'});
      const txt=await r.text();status(sec,r.ok&&txt==='OK'?'Renamed: '+to:txt,!r.ok);list(dir);};
    bDel.onclick=async()=>{
      if(!confirm('Delete '+f.name+(top&&lastOf(f.name)?' and the folder '+pbase(f.name)+'/ with its samples':'')+'?'))return;
      const r=await fetch('/api/delete?'+q,{method:'POST'});
      const txt=await r.text();status(sec,r.ok&&txt==='OK'?'Deleted: '+f.name:txt,!r.ok);list(dir);};
    t.appendChild(tr);}
  filter(dir);
}
// Rows whose name does not contain the filter text are hidden ("..": always shown).
function filter(dir){
  const sec=$('#'+dir),q=$('.tools input',sec).value.trim().toLowerCase();
  for(const tr of $('table',sec).rows){const a=$('a',tr);if(!a||a.textContent==='..')continue;
    tr.classList.toggle('hide',!!q&&!a.textContent.toLowerCase().includes(q));}
}
async function upload(dir,files){
  const sec=$('#'+dir),q=dq(dir);let ok=0;  // folder at the start: navigation meanwhile does not move the batch
  for(const f of files){
    status(sec,'Uploading '+f.name+'...');
    let r=await send('/api/upload?'+q+'&overwrite=0',f,sec);
    if(r.code===409&&confirm(f.name+' already exists. Replace it?'))r=await send('/api/upload?'+q+'&overwrite=1',f,sec);
    if(r.code===200)ok++;else if(r.code!==409){status(sec,f.name+': '+r.text,1);await list(dir);return;}
  }
  status(sec,'Files uploaded: '+ok);list(dir);
}
async function firmware(files){
  const sec=$('#fw'),f=files[0];if(!f)return;
  if(!/\.bin$/i.test(f.name)){status(sec,'a .bin file is needed',1);return;}
  if(!confirm('Flash '+f.name+' ('+kb(f.size)+')? The tracker will restart.'))return;
  status(sec,'Flashing...');
  const r=await send('/api/update',f,sec);
  if(r.code!==200){status(sec,r.text,1);return;}
  status(sec,'Done, the tracker is restarting. Wi-Fi mode is off after the restart.');
}
function drop(sec,fn){
  const d=$('.drop',sec),i=$('input',d);
  d.onclick=()=>i.click();i.onclick=e=>e.stopPropagation();
  i.onchange=()=>{fn([...i.files]);i.value='';};
  d.ondragover=e=>{e.preventDefault();d.classList.add('over');};
  d.ondragleave=()=>d.classList.remove('over');
  d.ondrop=e=>{e.preventDefault();d.classList.remove('over');fn([...e.dataTransfer.files]);};
}
const dirs=['midi','projects','samples','wavetables','presets','diag'];
for(const dir of dirs){if(dir!=='diag')drop($('#'+dir),fs=>upload(dir,fs));$('#'+dir+' .tools input').oninput=()=>filter(dir);}
for(const dir of ['samples','wavetables','presets'])$('#'+dir+' .crumb button').onclick=()=>mkdir(dir);
drop($('#fw'),firmware);
// One section at a time; its list loads when it is first shown (then on every visit, fresh).
function show(id){
  if(!$('#'+id))id='projects';
  for(const b of document.querySelectorAll('nav button'))b.classList.toggle('on',b.dataset.t===id);
  for(const sec of document.querySelectorAll('main section'))sec.classList.toggle('on',sec.id===id);
  if(dirs.includes(id))list(id);
  if(location.hash!=='#'+id)history.replaceState(null,'','#'+id);
}
for(const b of document.querySelectorAll('nav button'))b.onclick=()=>show(b.dataset.t);
show(location.hash.slice(1));
</script></body></html>)HTML";

}  // namespace net
