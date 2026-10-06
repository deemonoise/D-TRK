#pragma once
#include <pgmspace.h>

namespace net {

// Single page served at "/": file lists, upload (drag & drop), rename, delete, firmware update;
// samples, wavetables and presets also have subfolders (breadcrumb, new folder, delete an empty folder;
// presets sit in type folders FM / DRUM / SAMPLE / CHIP / SYNTH); projects have one level of project
// folders with their samples and a "wt" folder of wavetables (made and removed by the tracker, no new
// folder button).
// No external resources: works without internet access.
static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>D-TRK</title>
<style>
:root{--bg:#111;--fg:#eee;--dim:#888;--acc:#fe0;--box:#1d1d1d;--err:#f55}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
main{max-width:760px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}h2{font-size:17px;margin:24px 0 8px;color:var(--acc)}
p.note{color:var(--dim);margin:0 0 8px}
.drop{border:2px dashed #444;border-radius:8px;padding:16px;text-align:center;color:var(--dim);cursor:pointer}
.drop.over{border-color:var(--acc);color:var(--fg)}
table{width:100%;border-collapse:collapse;margin-top:8px}
td{padding:6px 4px;border-bottom:1px solid #222;word-break:break-all}
td.sz{color:var(--dim);white-space:nowrap;text-align:right;width:70px}
td.act{white-space:nowrap;text-align:right;width:1%}
a{color:var(--fg)}
button{background:var(--box);color:var(--fg);border:1px solid #444;border-radius:6px;padding:4px 10px;margin-left:4px;cursor:pointer}
button:hover{border-color:var(--acc)}
.st{min-height:1.4em;margin-top:8px;color:var(--dim)}.st.err{color:var(--err)}
progress{width:100%;display:none;margin-top:8px}
.crumb{margin:8px 0 0;display:flex;align-items:center;gap:4px;flex-wrap:wrap}.crumb a{color:var(--acc);cursor:pointer}
.crumb button{margin-left:auto}td.dir a{color:var(--acc);cursor:pointer}
</style></head><body><main>
<h1>D-TRK</h1>
<p class="note">Файлы на карте microSD. Пока открыта эта страница, трекер в режиме Wi-Fi.</p>

<section id="midi"><h2>MIDI (/midi)</h2>
<p class="note">Только .mid, до 512 КБ. Импорт на трекере: FILE &rarr; Import MIDI.</p>
<div class="drop">Перетащите файлы сюда или нажмите, чтобы выбрать<input type="file" accept=".mid" multiple hidden></div>
<progress max="100"></progress><div class="st"></div><table></table></section>

<section id="projects"><h2>Проекты (/projects)</h2>
<p class="note">.mtp и .bak, имя до 16 символов: латиница, цифры, - и _. В папке с именем проекта &mdash; его сэмплы (.wav до 10 МБ, имя до 16 символов), в её подпапке wt &mdash; волновые таблицы проекта: трекер пишет их при сохранении и подтягивает при загрузке проекта. Чтобы перенести проект, скопируйте .mtp и его папку целиком.</p>
<div class="crumb"><span></span></div>
<div class="drop">Перетащите файлы сюда или нажмите, чтобы выбрать<input type="file" accept=".mtp,.bak" multiple hidden></div>
<progress max="100"></progress><div class="st"></div><table></table></section>

<section id="samples"><h2>Сэмплы (/samples)</h2>
<p class="note">Только .wav, до 4 МБ. Импорт в банк &mdash; на трекере: FILE &rarr; SAMPLES. Можно раскладывать по папкам (до 4 уровней).</p>
<div class="crumb"><span></span><button>Новая папка</button></div>
<div class="drop">Перетащите файлы сюда или нажмите, чтобы выбрать<input type="file" accept=".wav" multiple hidden></div>
<progress max="100"></progress><div class="st"></div><table></table></section>

<section id="wavetables"><h2>Волновые таблицы (/wavetables)</h2>
<p class="note">Только .wav (PCM 8/16/24 бит; кадры по 2048 точек, как у Serum, по 256 или по чанку clm), до 3 МБ. Импорт &mdash; на трекере: INST SYNTH &rarr; таблица &rarr; IMPORT. Можно раскладывать по папкам (до 4 уровней).</p>
<div class="crumb"><span></span><button>Новая папка</button></div>
<div class="drop">Перетащите файлы сюда или нажмите, чтобы выбрать<input type="file" accept=".wav" multiple hidden></div>
<progress max="100"></progress><div class="st"></div><table></table></section>

<section id="presets"><h2>Пресеты (/presets)</h2>
<p class="note">Только .mti, до 1 КБ, имя до 16 символов: латиница, цифры, - и _. Пресет лежит в папке своего типа (FM, DRUM, SAMPLE, CHIP, SYNTH), внутри можно раскладывать по папкам (до 4 уровней). На трекере: INST &rarr; пресеты.</p>
<div class="crumb"><span></span><button>Новая папка</button></div>
<div class="drop">Перетащите файлы сюда или нажмите, чтобы выбрать<input type="file" accept=".mti" multiple hidden></div>
<progress max="100"></progress><div class="st"></div><table></table></section>

<section id="fw"><h2>Прошивка</h2>
<p class="note">Файл .pio/build/wt32/firmware.bin. После загрузки трекер перезагрузится.</p>
<div class="drop">Перетащите firmware.bin сюда или нажмите, чтобы выбрать<input type="file" accept=".bin" hidden></div>
<progress max="100"></progress><div class="st"></div></section>
</main>
<script>
const $=(s,e=document)=>e.querySelector(s);
const kb=n=>n<1024?n+' Б':n<1048576?(n/1024).toFixed(n<10240?1:0)+' КБ':(n/1048576).toFixed(1)+' МБ';
function status(sec,msg,err){const s=$('.st',sec);s.textContent=msg;s.className='st'+(err?' err':'');}
function send(url,file,sec){return new Promise(res=>{
  const x=new XMLHttpRequest(),f=new FormData(),pg=$('progress',sec);
  f.append('file',file,file.name);x.open('POST',url);
  pg.style.display='block';pg.value=0;
  x.upload.onprogress=e=>{if(e.lengthComputable)pg.value=e.loaded*100/e.total;};
  x.onload=()=>{pg.style.display='none';res({code:x.status,text:x.responseText});};
  x.onerror=()=>{pg.style.display='none';res({code:0,text:'нет связи с трекером'});};
  x.send(f);});}
// Subfolder per section: samples / wavetables / presets "" or "a/b", projects "", a project folder or its wt.
const sub={midi:'',projects:'',samples:'',wavetables:'',presets:''};
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
  const sec=$('#'+dir),n=prompt('Имя папки (латиница, цифры, пробел, . _ -)');if(!n)return;
  const r=await fetch('/api/mkdir?'+dq(dir)+'&name='+encodeURIComponent(n),{method:'POST'});
  status(sec,r.ok?'Папка создана: '+n:await r.text(),!r.ok);list(dir);
}
async function list(dir){
  const sec=$('#'+dir),t=$('table',sec);crumb(dir);
  let r;try{r=await fetch('/api/list?'+dq(dir));}catch(e){status(sec,'нет связи с трекером',1);return;}
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
  if(!files.length){t.insertAdjacentHTML('beforeend','<tr><td class="sz" style="text-align:left">пусто</td></tr>');return;}
  for(const f of files){
    const tr=document.createElement('tr'),q=dq(dir)+'&name='+encodeURIComponent(f.name);
    if(f.dir){
      // Project folders: made and removed by the tracker; preset type folders: the tracker needs them.
      const own=dir!=='projects',fixed=dir==='presets'&&!sub[dir],del=own&&!fixed;
      tr.innerHTML='<td class="dir"><a></a></td><td class="sz">'+(own?'папка':sub[dir]?'таблицы':'сэмплы')+'</td><td class="act">'+(del?'<button>Удалить</button>':'')+'</td>';
      const a=$('a',tr);a.textContent=f.name+'/';a.onclick=()=>go(dir,(sub[dir]?sub[dir]+'/':'')+f.name);
      if(del)$('button',tr).onclick=async()=>{if(!confirm('Удалить пустую папку '+f.name+'?'))return;
        const r=await fetch('/api/rmdir?'+q,{method:'POST'});
        status(sec,r.ok?'Папка удалена: '+f.name:await r.text(),!r.ok);list(dir);};
      t.appendChild(tr);continue;}
    tr.innerHTML='<td><a></a></td><td class="sz"></td><td class="act"><button>Имя</button><button>Удалить</button></td>';
    const a=$('a',tr);a.textContent=f.name;a.href='/api/file?'+q;a.download=f.name;
    $('.sz',tr).textContent=kb(f.size);
    const [bRen,bDel]=tr.querySelectorAll('button');
    bRen.onclick=async()=>{const to=prompt('Новое имя',f.name);if(!to||to===f.name)return;
      const r=await fetch('/api/rename?'+dq(dir)+'&from='+encodeURIComponent(f.name)+'&to='+encodeURIComponent(to),{method:'POST'});
      const txt=await r.text();status(sec,r.ok&&txt==='OK'?'Переименован: '+to:txt,!r.ok);list(dir);};
    bDel.onclick=async()=>{
      if(!confirm('Удалить '+f.name+(top&&lastOf(f.name)?' и папку '+pbase(f.name)+'/ с сэмплами':'')+'?'))return;
      const r=await fetch('/api/delete?'+q,{method:'POST'});
      const txt=await r.text();status(sec,r.ok&&txt==='OK'?'Удалён: '+f.name:txt,!r.ok);list(dir);};
    t.appendChild(tr);}
}
async function upload(dir,files){
  const sec=$('#'+dir),q=dq(dir);let ok=0;  // folder at the start: navigation meanwhile does not move the batch
  for(const f of files){
    status(sec,'Загрузка '+f.name+'...');
    let r=await send('/api/upload?'+q+'&overwrite=0',f,sec);
    if(r.code===409&&confirm(f.name+' уже есть. Заменить?'))r=await send('/api/upload?'+q+'&overwrite=1',f,sec);
    if(r.code===200)ok++;else if(r.code!==409){status(sec,f.name+': '+r.text,1);await list(dir);return;}
  }
  status(sec,'Загружено файлов: '+ok);list(dir);
}
async function firmware(files){
  const sec=$('#fw'),f=files[0];if(!f)return;
  if(!/\.bin$/i.test(f.name)){status(sec,'нужен файл .bin',1);return;}
  if(!confirm('Прошить '+f.name+' ('+kb(f.size)+')? Трекер перезагрузится.'))return;
  status(sec,'Прошивка...');
  const r=await send('/api/update',f,sec);
  if(r.code!==200){status(sec,r.text,1);return;}
  status(sec,'Готово, трекер перезагружается. Режим Wi-Fi после перезагрузки выключен.');
}
function drop(sec,fn){
  const d=$('.drop',sec),i=$('input',d);
  d.onclick=()=>i.click();i.onclick=e=>e.stopPropagation();
  i.onchange=()=>{fn([...i.files]);i.value='';};
  d.ondragover=e=>{e.preventDefault();d.classList.add('over');};
  d.ondragleave=()=>d.classList.remove('over');
  d.ondrop=e=>{e.preventDefault();d.classList.remove('over');fn([...e.dataTransfer.files]);};
}
for(const dir of ['midi','projects','samples','wavetables','presets']){drop($('#'+dir),fs=>upload(dir,fs));list(dir);}
for(const dir of ['samples','wavetables','presets'])$('#'+dir+' .crumb button').onclick=()=>mkdir(dir);
drop($('#fw'),firmware);
</script></body></html>)HTML";

}  // namespace net
