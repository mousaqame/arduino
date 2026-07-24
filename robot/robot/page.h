// Control page served by the robot itself, stored in flash (PROGMEM).
// Mobile-first: big touch targets, no external assets, no CDN — the robot has
// no internet connection to fetch anything from.
#pragma once

#include <pgmspace.h>

const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<meta name="theme-color" content="#0d1418">
<title>Robot</title>
<style>
  :root{
    --bg:#0d1418; --card:#141f24; --sunk:#0f181c; --line:#22323a;
    --ink:#e2edf0; --mut:#8ba3ac; --accent:#2dd4bf; --warn:#f59e0b; --stop:#ef4444;
  }
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  body{
    margin:0;background:var(--bg);color:var(--ink);
    font:16px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
    padding:14px 14px 34px;max-width:560px;margin:0 auto;
  }
  header{display:flex;align-items:center;gap:10px;padding:6px 2px 16px}
  h1{margin:0;font-size:1.15rem;font-weight:660;letter-spacing:-.02em}
  .grow{flex:1}
  .dot{width:9px;height:9px;border-radius:50%;background:var(--stop)}
  .dot.on{background:var(--accent);box-shadow:0 0 0 3px rgba(45,212,191,.18)}
  .who{font-size:12px;color:var(--mut)}

  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:15px;margin-bottom:12px}
  h2{margin:0 0 12px;font-size:10.5px;letter-spacing:.14em;text-transform:uppercase;color:var(--mut);font-weight:650}

  .joint{margin-bottom:16px}
  .joint:last-child{margin-bottom:0}
  .jrow{display:flex;align-items:baseline;gap:8px;margin-bottom:7px}
  .jname{font-weight:600}
  .jval{margin-left:auto;font-variant-numeric:tabular-nums;color:var(--accent);font-weight:650}
  .jval span{color:var(--mut);font-weight:400;font-size:12px}

  input[type=range]{
    -webkit-appearance:none;appearance:none;width:100%;height:34px;
    background:transparent;margin:0;
  }
  input[type=range]::-webkit-slider-runnable-track{height:10px;border-radius:5px;background:var(--sunk);border:1px solid var(--line)}
  input[type=range]::-moz-range-track{height:10px;border-radius:5px;background:var(--sunk);border:1px solid var(--line)}
  input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none;width:30px;height:30px;border-radius:50%;
    background:var(--accent);border:3px solid var(--card);margin-top:-11px;
  }
  input[type=range]::-moz-range-thumb{
    width:26px;height:26px;border-radius:50%;background:var(--accent);border:3px solid var(--card)
  }

  .btns{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}
  button{
    font:inherit;font-weight:600;color:var(--ink);background:var(--sunk);
    border:1px solid var(--line);border-radius:11px;padding:15px 8px;cursor:pointer;
    touch-action:manipulation;
  }
  button:active{background:var(--accent);color:#062b27;border-color:var(--accent)}
  button.wide{grid-column:span 3}
  button.danger{color:var(--stop);border-color:#43242a}
  button.danger:active{background:var(--stop);color:#fff}
  button:disabled{opacity:.4}

  .speed{display:flex;align-items:center;gap:12px}
  .speed span{font-size:13px;color:var(--mut);white-space:nowrap}
  .speed input{flex:1}

  .msg{font-size:12.5px;color:var(--mut);text-align:center;min-height:18px;padding-top:4px}
  .msg.err{color:var(--warn)}
</style>
</head>
<body>

<header>
  <span class="dot" id="dot"></span>
  <h1>Robot</h1>
  <div class="grow"></div>
  <span class="who" id="who">connecting…</span>
</header>

<div class="card">
  <h2>Moves</h2>
  <div class="btns" id="moves"></div>
  <div class="btns" style="margin-top:9px">
    <button class="wide danger" data-act="stop">Stop</button>
  </div>
</div>

<div class="card">
  <h2>Each joint</h2>
  <div id="joints"></div>
</div>

<div class="card">
  <h2>Settings</h2>
  <div class="speed">
    <span>Speed</span>
    <input type="range" id="speed" min="20" max="300" step="10" value="120">
    <span id="speedv">120&deg;/s</span>
  </div>
  <div class="btns" style="margin-top:12px">
    <button class="wide" id="gentle">Low-power mode: off</button>
    <button class="wide" data-act="relax">Relax motors</button>
  </div>
  <p class="msg" style="text-align:left;margin-top:6px">
    Turn low-power mode on if your power supply is small. Motors then take turns
    instead of all moving at once.
  </p>
</div>

<p class="msg" id="msg"></p>

<script>
const $=id=>document.getElementById(id);
let joints=[], dragging=null, timer=null, gentleOn=false;

async function api(path, params){
  const q = params ? '?' + new URLSearchParams(params) : '';
  const r = await fetch('/api/' + path + q, {method:'POST'});
  if(!r.ok) throw new Error(r.status);
  return r.json();
}

function note(t, err){ const m=$('msg'); m.textContent=t||''; m.className='msg'+(err?' err':''); }

function render(){
  if($('joints').children.length !== joints.length){
    $('joints').innerHTML = joints.map((j,i)=>`
      <div class="joint">
        <div class="jrow">
          <span class="jname">${j.name}</span>
          <span class="jval" id="v${i}">${j.target}<span>&deg;</span></span>
        </div>
        <input type="range" id="s${i}" min="${j.min}" max="${j.max}" value="${j.target}">
      </div>`).join('');
    joints.forEach((j,i)=>{
      const s=$('s'+i);
      s.addEventListener('input',()=>{ dragging=i; $('v'+i).innerHTML=s.value+'<span>&deg;</span>'; });
      s.addEventListener('change',async()=>{
        try{ await api('joint',{name:j.name,angle:s.value}); note(''); }
        catch{ note('Could not reach the robot',1); }
        dragging=null;
      });
    });
  } else {
    joints.forEach((j,i)=>{
      if(dragging===i) return;
      $('s'+i).value=j.target;
      $('v'+i).innerHTML=j.target+'<span>&deg;</span>';
    });
  }
}

async function poll(){
  try{
    const r=await fetch('/api/state');
    const d=await r.json();
    joints=d.joints; render();
    $('dot').classList.add('on');
    $('who').textContent=d.where;
    $('speed').disabled=false;
    gentleOn=!!d.gentle;
    $('gentle').textContent='Low-power mode: '+(gentleOn?'on':'off');
    $('gentle').style.borderColor=gentleOn?'var(--accent)':'';
    $('gentle').style.color=gentleOn?'var(--accent)':'';
  }catch{
    $('dot').classList.remove('on');
    $('who').textContent='no connection';
  }
}

async function loadMoves(){
  try{
    const list = await (await fetch('/api/moves')).json();
    $('moves').innerHTML = list.map(m=>
      `<button data-pose="${m.name}">${m.label}</button>`).join('');
    document.querySelectorAll('[data-pose]').forEach(b=>b.onclick=async()=>{
      try{ await api('pose',{name:b.dataset.pose}); note(b.textContent+'…'); poll(); }
      catch{ note('Could not reach the robot',1); }
    });
  }catch{ note('Could not load the moves',1); }
}
loadMoves();
document.querySelectorAll('[data-act]').forEach(b=>b.onclick=async()=>{
  try{ await api(b.dataset.act); note(b.textContent); poll(); }
  catch{ note('Could not reach the robot',1); }
});

$('gentle').onclick=async()=>{
  try{ await api('power',{mode:gentleOn?'normal':'gentle'}); poll();
       note(gentleOn?'Back to normal':'Motors will take turns'); }
  catch{ note('Could not reach the robot',1); }
};

$('speed').addEventListener('input',()=>$('speedv').innerHTML=$('speed').value+'&deg;/s');
$('speed').addEventListener('change',async()=>{
  try{ await api('speed',{dps:$('speed').value}); }catch{ note('Could not reach the robot',1); }
});

poll();
setInterval(poll, 700);
</script>
</body>
</html>
)rawliteral";
