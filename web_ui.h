#pragma once
#include <Arduino.h>

// Trang quan ly:
//  - Xem/gui/xoa ma da luu
//  - Danh sach ma built-in + nut gui thu
//  - Thu IR bang web (chon protocol, nhap gia tri hex, bam Gui Thu - KHONG luu)
//  - Cai dat chan phat IR, LED on/off
//  - TV-B-GONE tu xa (2 che do)
//  - IR JAMMER: phat IR nhieu 38kHz chan remote TV
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TV Killer Pro v3</title>
<style>
  :root{--bg:#0f1115;--card:#1a1e27;--accent:#7c5cff;--accent2:#00d4b0;--text:#eee;--muted:#8a90a2;}
  *{box-sizing:border-box;}
  body{background:var(--bg);color:var(--text);font-family:system-ui,Segoe UI,Roboto,sans-serif;margin:0;padding:16px;}
  h1{font-size:20px;margin:0 0 4px;background:linear-gradient(90deg,var(--accent),var(--accent2));-webkit-background-clip:text;background-clip:text;color:transparent;}
  .sub{color:var(--muted);font-size:12px;margin-bottom:16px;}
  .card{background:var(--card);border-radius:14px;padding:14px;margin-bottom:14px;box-shadow:0 4px 14px rgba(0,0,0,.3);}
  .card h2{font-size:14px;margin:0 0 10px;color:var(--accent2);}
  .row{display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px solid #262b36;}
  .row:last-child{border-bottom:none;}
  .name{font-weight:600;}
  .meta{color:var(--muted);font-size:12px;}
  button{border:none;border-radius:8px;padding:8px 12px;font-size:13px;cursor:pointer;font-weight:600;}
  .btn-send{background:var(--accent2);color:#00201a;}
  .btn-del{background:#ff5468;color:#3a0008;margin-left:6px;}
  .btn-main{background:linear-gradient(90deg,var(--accent),var(--accent2));color:#0a0a10;width:100%;padding:12px;font-size:15px;border-radius:12px;margin-bottom:8px;}
  .btn-sec{background:#2a3040;color:var(--text);width:100%;padding:12px;font-size:14px;border-radius:12px;margin-bottom:8px;}
  .btn-pin{background:#2a3040;color:var(--text);flex:1;}
  .btn-pin.active{background:linear-gradient(90deg,var(--accent),var(--accent2));color:#0a0a10;}
  .btn-test{background:#ff9f1c;color:#2a1a00;padding:10px 14px;}
  .btn-toggle{background:#2a3040;color:var(--text);padding:8px 16px;}
  .btn-toggle.on{background:#00d4b0;color:#002a22;}
  .btn-jammer{background:linear-gradient(90deg,#ff2244,#ff7700);color:#fff;width:100%;padding:14px;font-size:16px;border-radius:12px;margin-bottom:8px;letter-spacing:.5px;}
  .btn-jammer.on{background:linear-gradient(90deg,#222,#444);color:#ff5468;border:2px solid #ff5468;}
  input,select,textarea{width:100%;background:#11141b;border:1px solid #2a3040;color:var(--text);border-radius:8px;padding:8px;margin-bottom:8px;font-size:13px;}
  label{font-size:12px;color:var(--muted);display:block;margin-bottom:2px;}
  .grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px;}
  .toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#222;padding:10px 16px;border-radius:20px;font-size:13px;opacity:0;transition:opacity .3s;pointer-events:none;z-index:999;}
  .toast.show{opacity:1;}
  .empty{color:var(--muted);font-size:13px;text-align:center;padding:14px;}
  .badge{display:inline-block;background:#2a3040;color:var(--muted);border-radius:4px;padding:1px 6px;font-size:11px;margin-left:4px;}
  .section-note{color:var(--muted);font-size:11px;margin-top:6px;}
  #rawTestFields{display:none;}
  .flex-gap{display:flex;gap:8px;flex-wrap:wrap;}
  .jammer-warn{color:#ff9f1c;font-size:11px;margin-top:8px;line-height:1.4;}
  .jammer-live{color:#ff5468;font-size:13px;font-weight:600;margin-top:6px;animation:pulse 1s infinite;}
  @keyframes pulse{0%,100%{opacity:1;}50%{opacity:.35;}}
</style>
</head>
<body>
  <h1>⚡ TV Killer Pro <span style="font-size:13px;color:var(--muted);background:none;-webkit-background-clip:unset;background-clip:unset;">v3</span></h1>
  <div class="sub">Ket noi AP: <b>TV-Killer</b> / <b>12345678</b></div>

  <!-- SETTINGS -->
  <div class="card">
    <h2>⚙️ Cai dat</h2>
    <label>Chan phat IR</label>
    <div class="flex-gap" style="margin-bottom:10px;">
      <button class="btn-pin" id="pinBtn5" onclick="setPin(5)">D5</button>
      <button class="btn-pin" id="pinBtn7" onclick="setPin(7)">D7</button>
    </div>
    <label>LED WS2812B</label>
    <div class="flex-gap">
      <button class="btn-toggle" id="ledBtn" onclick="toggleLed()">⚫ TAT</button>
    </div>
    <div class="section-note">Tat ca cai dat duoc luu vao flash, giu nguyen sau khi mat dien.</div>
  </div>

  <!-- TV-B-GONE -->
  <div class="card">
    <h2>📡 TV-B-GONE</h2>
    <button class="btn-main" onclick="startBlast('saved')">🔥 Ban nhanh (Built-in + Da luu)</button>
    <button class="btn-sec" onclick="startBlast('world')">🌍 World IR Codes (NA + EU)</button>
    <div class="meta" id="blastStatus" style="margin-top:4px;"></div>
  </div>

  <!-- IR JAMMER -->
  <div class="card">
    <h2>🚫 IR JAMMER</h2>
    <p style="font-size:12px;color:var(--muted);margin:0 0 10px;">
      Phat IR nhieu 38kHz lien tuc de chan remote TV trong tam phu song cua LED IR.
      Khi bat, TV se khong nhan duoc lenh tu bat ky remote nao.
    </p>
    <button class="btn-jammer" id="jammerBtn" data-on="0" onclick="toggleJammer()">
      ▶ BAT JAMMER
    </button>
    <div id="jammerStatus"></div>
    <div class="jammer-warn">
      ⚠️ Chi dung cho muc dich nghien cuu. Khong dung gay nhieu thiet bi nguoi khac.
    </div>
  </div>

  <!-- THU IR QUA WEB -->
  <div class="card">
    <h2>🧪 Thu IR bang web</h2>
    <p style="font-size:12px;color:var(--muted);margin:0 0 10px;">Chon protocol va nhap gia tri hex de phat thu (KHONG luu). Dung de kiem tra ma truoc khi luu chinh thuc qua OLED.</p>
    <label>Protocol</label>
    <select id="t_type" onchange="toggleTestType()">
      <option>NEC</option><option>SAMSUNG</option><option>SONY</option>
      <option>LG</option><option>PANASONIC</option><option>SHARP</option>
      <option>RC5</option><option>RC6</option><option>JVC</option>
      <option>RCA</option><option>SANYO</option><option>DENON</option>
      <option>WHYNTER</option><option>MITSUBISHI</option>
      <option>RAW</option>
    </select>
    <div id="stdTestFields" class="grid2">
      <div><label>Gia tri (hex, vd: 0x20DF10EF)</label><input id="t_value" placeholder="0x..."></div>
      <div><label>So bit</label><input id="t_bits" type="number" value="32"></div>
    </div>
    <div id="rawTestFields">
      <label>Tan so (kHz)</label>
      <input id="t_freq" type="number" value="38">
      <label>Mang RAW (microseconds, cach nhau dau phay)</label>
      <textarea id="t_raw" rows="2" placeholder="9000,4500,560,560,..."></textarea>
    </div>
    <button class="btn-test" style="width:100%;" onclick="testIr()">▶ Phat Thu (khong luu)</button>
  </div>

  <!-- MA DA LUU -->
  <div class="card">
    <h2>💾 Ma da luu <span class="badge" id="savedCount">0</span></h2>
    <div id="savedList" class="empty">Dang tai...</div>
  </div>

  <!-- MA BUILT-IN -->
  <div class="card">
    <h2>📋 Ma Built-in <span class="badge" id="builtinCount"></span></h2>
    <div id="builtinList" class="empty">Dang tai...</div>
  </div>

  <div class="toast" id="toast"></div>

<script>
function toast(msg,dur=1800){
  const t=document.getElementById('toast');
  t.textContent=msg; t.classList.add('show');
  setTimeout(()=>t.classList.remove('show'), dur);
}
function toggleTestType(){
  const v = document.getElementById('t_type').value;
  document.getElementById('stdTestFields').style.display = v==='RAW' ? 'none' : 'grid';
  document.getElementById('rawTestFields').style.display = v==='RAW' ? 'block' : 'none';
}
async function loadSaved(){
  const res = await fetch('/api/codes'); const data = await res.json();
  document.getElementById('savedCount').textContent = data.length;
  const el = document.getElementById('savedList');
  if(!data.length){el.innerHTML='<div class="empty">Chua co ma nao. Dung OLED che do "LUU IR MOI".</div>';return;}
  el.innerHTML = data.map((c,i)=>{
    let meta;
    if(c.type==='RAW') meta = c.type+' - '+c.raw.length+' xung';
    else if(c.state) meta = c.type+' (AC) - '+c.bytes+' byte';
    else meta = c.type+' - '+c.value;
    return `<div class="row">
      <div><div class="name">${c.name}</div><div class="meta">${meta}</div></div>
      <div>
        <button class="btn-send" onclick="sendSaved(${i})">Gui</button>
        <button class="btn-del" onclick="delSaved(${i})">Xoa</button>
      </div>
    </div>`;
  }).join('');
}
async function loadBuiltin(){
  const res = await fetch('/api/builtin'); const data = await res.json();
  document.getElementById('builtinCount').textContent = data.length;
  const el = document.getElementById('builtinList');
  el.innerHTML = data.map((c,i)=>`<div class="row">
    <div><div class="name">${c.brand}</div><div class="meta">${c.protocol}</div></div>
    <div><button class="btn-send" onclick="sendBuiltin(${i})">Gui</button></div>
  </div>`).join('');
}
async function sendSaved(i){await fetch('/api/send?index='+i,{method:'POST'});toast('Da gui!');}
async function delSaved(i){await fetch('/api/delete?index='+i,{method:'POST'});toast('Da xoa');loadSaved();}
async function sendBuiltin(i){await fetch('/api/send_builtin?index='+i,{method:'POST'});toast('Da gui!');}
async function startBlast(mode){
  await fetch('/api/tvbgone/start?mode='+mode,{method:'POST'});
  toast(mode==='world'?'Bat dau WORLD IR CODES!':'Bat dau ban nhanh!',2000);
  pollStatus();
}
async function setPin(p){
  await fetch('/api/txpin?pin='+p,{method:'POST'});
  toast('Da chuyen sang D'+p);
  refreshPinButtons(p);
}
function refreshPinButtons(p){
  document.getElementById('pinBtn5').classList.toggle('active',p===5||p==='5');
  document.getElementById('pinBtn7').classList.toggle('active',p===7||p==='7');
}
async function toggleLed(){
  const btn = document.getElementById('ledBtn');
  const isOn = btn.classList.contains('on');
  const newState = isOn ? '0' : '1';
  await fetch('/api/led?state='+newState,{method:'POST'});
  refreshLedButton(!isOn);
  toast(isOn ? 'Da tat LED' : 'Da bat LED');
}
function refreshLedButton(on){
  const btn = document.getElementById('ledBtn');
  btn.classList.toggle('on', on);
  btn.textContent = on ? '💡 BAT' : '⚫ TAT';
}
async function testIr(){
  const type = document.getElementById('t_type').value;
  let body = {type};
  if(type==='RAW'){
    body.freq = parseInt(document.getElementById('t_freq').value)||38;
    body.raw = document.getElementById('t_raw').value.split(',').map(s=>parseInt(s.trim())).filter(n=>!isNaN(n));
    if(!body.raw.length){toast('Nhap gia tri RAW!');return;}
  } else {
    const val = document.getElementById('t_value').value.trim();
    if(!val){toast('Nhap gia tri hex!');return;}
    body.value = val;
    body.bits = parseInt(document.getElementById('t_bits').value)||32;
  }
  const res = await fetch('/api/test_ir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  if(res.ok) toast('Da phat thu IR!'); else toast('Loi khi phat!');
}

// ==================== IR JAMMER ====================
async function toggleJammer(){
  const btn = document.getElementById('jammerBtn');
  const isOn = btn.dataset.on === '1';
  const newState = isOn ? '0' : '1';
  try {
    const res = await fetch('/api/jammer?state=' + newState, {method:'POST'});
    if(!res.ok){ toast('Loi: khong gui duoc lenh!'); return; }
    updateJammerUI(newState === '1');
    toast(newState === '1' ? '🔴 JAMMER DANG BAT!' : 'Da tat jammer', 2200);
  } catch(e) {
    toast('Loi ket noi!');
  }
}
function updateJammerUI(on){
  const btn = document.getElementById('jammerBtn');
  const st  = document.getElementById('jammerStatus');
  btn.dataset.on = on ? '1' : '0';
  if (on) {
    btn.textContent = '⏹ TAT JAMMER';
    btn.classList.add('on');
    st.innerHTML = '<div class="jammer-live">🔴 Dang phat IR nhieu 38kHz...</div>';
  } else {
    btn.textContent = '▶ BAT JAMMER';
    btn.classList.remove('on');
    st.innerHTML = '';
  }
}

async function pollStatus(){
  try {
    const res = await fetch('/api/status');
    const s = await res.json();
    const el = document.getElementById('blastStatus');
    if(s.blastTotal && s.blastIndex < s.blastTotal)
      el.textContent = 'Tien do: '+s.blastIndex+'/'+s.blastTotal;
    else el.textContent = '';
    refreshPinButtons(s.txPin);
    refreshLedButton(s.ledEnabled);
    if (s.jamming !== undefined) updateJammerUI(s.jamming);
  } catch(e) { /* bo qua loi mang */ }
}

// Init
toggleTestType();
loadSaved();
loadBuiltin();
pollStatus();
setInterval(pollStatus, 2000);
</script>
</body>
</html>
)HTMLPAGE";