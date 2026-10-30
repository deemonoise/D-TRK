#pragma once
#include <pgmspace.h>

namespace net {

// Single page served at "/" (and "/firmware"): firmware updates of the tracker (.bin) and of the synth
// board (.hex, through its card), and the Export section: the WAV files of /export on the synth board's
// card (FILE -> Export...), and a prepared Dice MIDI library uploaded to its fixed SD path.
// No external resources: works without internet access.
static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>D-TRK</title>
<style>
:root{--bg:#111;--fg:#eee;--dim:#888;--acc:#fe0;--box:#1d1d1d;--line:#2a2a2a;--err:#f55}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
header{border-bottom:1px solid var(--line)}
.bar{max-width:820px;margin:0 auto;padding:12px 16px 0}
h1{font-size:18px;margin:0 0 2px}header p{color:var(--dim);margin:0 0 8px;font-size:13px}
main{max-width:820px;margin:0 auto;padding:12px 16px 16px}
h2{font-size:16px;margin:0 0 4px}
p.note{color:var(--dim);margin:0 0 8px;font-size:13px}
.drop{border:2px dashed #444;border-radius:8px;padding:10px;text-align:center;color:var(--dim);cursor:pointer}
.drop.over{border-color:var(--acc);color:var(--fg)}
.st{min-height:1.4em;margin-top:8px;color:var(--dim)}.st.err{color:var(--err)}
progress{width:100%;display:none;margin-top:8px}
a{color:var(--acc)}ul.ex{list-style:none;padding:0;margin:4px 0 0}ul.ex li{display:flex;justify-content:space-between;
gap:12px;padding:2px 0;border-bottom:1px solid var(--line);font-size:14px}ul.ex li span{color:var(--dim)}
h3{font-size:14px;margin:10px 0 0;color:var(--dim)}button{background:var(--box);color:var(--fg);border:1px solid #444;
border-radius:6px;padding:4px 10px;cursor:pointer}
</style></head><body>
<header><div class="bar">
<h1>D-TRK</h1>
<p>While this page is open the tracker is in Wi-Fi mode.</p>
</div></header>
<main>
<section id="fw"><h2>Tracker (.bin)</h2>
<p class="note">The file .pio/build/wt32/firmware.bin. The tracker restarts after the update.</p>
<div class="drop">Drop firmware.bin here or click to choose<input type="file" accept=".bin" hidden></div>
<progress max="100"></progress><div class="st"></div></section>
<section id="syn" style="margin-top:20px"><h2>Synth board (.hex)</h2>
<p class="note">The file .pio/build/teensy41/firmware.hex. It goes to the synth board's card as
/firmware/teensy.hex, then the board flashes it and restarts.</p>
<div class="drop">Drop firmware.hex here or click to choose<input type="file" accept=".hex" hidden></div>
<progress max="100"></progress><div class="st"></div></section>
<section id="dice" style="margin-top:20px"><h2>Dice MIDI library (.dml)</h2>
<p class="note">Choose the prepared dice-library.dml. It replaces the library on the card after validation;
the previous file is kept as a backup. No restart is needed.</p>
<div class="drop">Drop dice-library.dml here or click to choose<input type="file" accept=".dml" hidden></div>
<progress max="100"></progress><div class="st"></div></section>
<section id="ex" style="margin-top:20px"><h2>Export (.wav)</h2>
<p class="note">Files of FILE &rarr; Export... on the synth board's card, /export/&lt;project&gt;/&lt;source&gt;/.
The link runs at about 200 KB/s (a 4 min master at 24 bit, 63 MB, takes ~5 min); for a full set of stems take
the card out. A cut download resumes where it stopped.</p>
<button>Refresh</button><div class="st"></div><div class="list"></div></section>
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
async function firmware(files){
  const sec=$('#fw'),f=files[0];if(!f)return;
  if(!/\.bin$/i.test(f.name)){status(sec,'a .bin file is needed',1);return;}
  if(!confirm('Flash '+f.name+' ('+kb(f.size)+')? The tracker will restart.'))return;
  status(sec,'Flashing...');
  const r=await send('/api/update',f,sec);
  if(r.code!==200){status(sec,r.text,1);return;}
  status(sec,'Done, the tracker is restarting. Wi-Fi mode is off after the restart.');
}
async function synth(files){
  const sec=$('#syn'),f=files[0];if(!f)return;
  if(!/\.hex$/i.test(f.name)){status(sec,'a .hex file is needed',1);return;}
  if(!confirm('Flash the synth board with '+f.name+' ('+kb(f.size)+')?'))return;
  status(sec,'Uploading to the card...');
  const r=await send('/api/update-synth',f,sec);
  if(r.code!==200){status(sec,r.text,1);return;}
  const pg=$('progress',sec);pg.style.display='block';pg.value=0;
  for(;;){
    await new Promise(t=>setTimeout(t,1000));
    let s;try{s=await(await fetch('/api/update-synth/status')).json();}catch(e){continue;}
    if(s.state==='flash'){pg.value=s.pct;status(sec,'Checking and staging... '+s.pct+'%');}
    else if(s.state==='reboot'){pg.value=100;status(sec,'Flashed, the synth board restarts...');}
    else if(s.state==='done'){pg.style.display='none';status(sec,'Done: '+s.msg);return;}
    else if(s.state==='error'){pg.style.display='none';status(sec,s.msg,1);return;}
  }
}
async function diceLibrary(files){
  const sec=$('#dice'),f=files[0];if(!f)return;
  if(f.name!=='dice-library.dml'){status(sec,'choose dice-library.dml',1);return;}
  if(f.size<16||f.size>4*1024*1024){status(sec,'the library must be 16 bytes to 4 MB',1);return;}
  status(sec,'Uploading and validating the library...');
  const r=await send('/api/dice-library',f,sec);
  status(sec,r.code===200?'Ready. Select MIDI or HYBRID in DICE.':r.text,r.code!==200);
}
function drop(sec,fn){
  const d=$('.drop',sec),i=$('input',d);
  d.onclick=()=>i.click();i.onclick=e=>e.stopPropagation();
  i.onchange=()=>{fn([...i.files]);i.value='';};
  d.ondragover=e=>{e.preventDefault();d.classList.add('over');};
  d.ondragleave=()=>d.classList.remove('over');
  d.ondrop=e=>{e.preventDefault();d.classList.remove('over');fn([...e.dataTransfer.files]);};
}
drop($('#fw'),firmware);
drop($('#syn'),synth);
drop($('#dice'),diceLibrary);
async function exports(){
  const sec=$('#ex'),l=$('.list',sec);status(sec,'Reading the card...');
  let a;try{a=await(await fetch('/api/export/list')).json();}catch(e){status(sec,'no answer',1);return;}
  if(!a.length){l.innerHTML='';status(sec,'No exports on the card.');return;}
  const g={};for(const f of a){const d=f.p.slice(0,f.p.lastIndexOf('/'));(g[d]=g[d]||[]).push(f);}
  let h='';for(const d of Object.keys(g).sort()){h+='<h3>'+d+'</h3><ul class="ex">';
    for(const f of g[d].sort((x,y)=>x.p<y.p?-1:1)){const n=f.p.slice(d.length+1);
      h+='<li><a href="/api/export/file?path='+encodeURIComponent(f.p)+'" download="'+n+'">'+n+'</a><span>'+kb(f.s)+'</span></li>';}
    h+='</ul>';}
  l.innerHTML=h;status(sec,a.length+' files');
}
$('#ex button').onclick=exports;exports();
</script></body></html>)HTML";

}  // namespace net
