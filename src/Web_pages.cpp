#include <Arduino.h>
#include "Web_pages.h"

// Auto-generated at patch time.
// NOTE: You can still serve /data/www versions via LittleFS if you prefer,
// but this file ensures INDEX_HTML / CONSOLE_HTML / OTA_HTML symbols exist.

const char INDEX_HTML[] PROGMEM = R"K2HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>K2UartBriage</title>
<style>
:root{
  --k2-bg:#0B0F14;
  --k2-panel:#121922;
  --k2-panel-alt:#0F151D;
  --k2-border:#223040;
  --k2-border-soft:#1A2431;
  --k2-text:#E6E6E6;
  --k2-text-dim:#9FB0C3;
  --k2-accent:#FF6A00;
  --k2-accent-hover:#FF8A2A;
  --k2-danger:#FF4D4D;
  --k2-console:#070B10;
  --k2-console-text:#D7E3F1;
  --radius:10px;
  --pad:14px;
  --font-ui:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;
  --font-mono:ui-monospace,SFMono-Regular,Consolas,"Liberation Mono",Menlo,monospace;
}
*{ box-sizing:border-box; }
html,body{ height:100%; }
body{ margin:0; background:var(--k2-bg); color:var(--k2-text); font-family:var(--font-ui); }
a{ color:var(--k2-text-dim); text-decoration:none; }
a:hover{ color:var(--k2-text); }
.small{ color:var(--k2-text-dim); font-size:12px; }
.mono{ font-family:var(--font-mono); white-space:pre-wrap; }
code{ font-family:var(--font-mono); background:#0B1118; border:1px solid var(--k2-border-soft); padding:2px 6px; border-radius:8px; color:var(--k2-console-text); }
.k2-topbar{ padding:12px 16px; border-bottom:1px solid var(--k2-border-soft); background:linear-gradient(180deg, rgba(18,25,34,.9), rgba(11,15,20,.0)); position:sticky; top:0; z-index:10; backdrop-filter: blur(4px); }
.k2-toprow{ display:flex; align-items:center; justify-content:space-between; gap:12px; }
.k2-brand{ display:flex; flex-direction:column; line-height:1.1; }
.k2-brand b{ font-size:14px; font-weight:650; }
.k2-brand span{ font-size:12px; color:var(--k2-text-dim); }
.k2-subrow{ display:flex; align-items:center; justify-content:space-between; gap:10px; margin-top:10px; flex-wrap:wrap; }
.k2-tabs{ display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
.k2-tab{ display:inline-flex; align-items:center; justify-content:center; gap:8px; padding:7px 10px; border-radius:999px; border:1px solid var(--k2-border); background:rgba(18,25,34,.6); color:var(--k2-text-dim); font-size:12px; white-space:nowrap; cursor:pointer; user-select:none; }
.k2-tab:hover{ background:rgba(18,25,34,.95); color:var(--k2-text); border-color:#2b3d52; }
.k2-tab.active{ background:rgba(255,106,0,.18); border-color:#7a3b12; color:var(--k2-text); }
.k2-pill{ display:inline-flex; align-items:center; gap:8px; padding:6px 10px; border-radius:999px; border:1px solid var(--k2-border); background:var(--k2-panel); color:var(--k2-text-dim); font-size:12px; white-space:nowrap; }
.k2-dot{ width:8px; height:8px; border-radius:50%; background:#555; }
.k2-pill.ok{ color:var(--k2-text); border-color:#28415a; }
.k2-pill.ok .k2-dot{ background:#3bd16f; }
.k2-pill.warn{ border-color:#5a3a28; color:var(--k2-text); }
.k2-pill.warn .k2-dot{ background:var(--k2-accent); }
.k2-pill.bad{ border-color:#5a2830; color:var(--k2-text); }
.k2-pill.bad .k2-dot{ background:var(--k2-danger); }
.k2-wrap{ max-width:1100px; margin:0 auto; padding:16px; }
.k2-row{ display:flex; gap:12px; flex-wrap:wrap; align-items:stretch; }
.k2-col{ flex:1 1 420px; min-width:320px; }
.k2-card{ background:var(--k2-panel); border:1px solid var(--k2-border); border-radius:var(--radius); padding:var(--pad); margin:0 0 12px 0; }
.k2-card.alt{ background:var(--k2-panel-alt); }
.k2-title{ font-size:14px; letter-spacing:.2px; margin:0 0 10px 0; }
.k2-grid{ display:grid; grid-template-columns: 1fr; gap:10px; margin-top:10px; }
@media (min-width:900px){ .k2-grid.two{ grid-template-columns:1fr 1fr; } }
.k2-input,.k2-select{ width:100%; background:#0B1118; color:var(--k2-text); border:1px solid var(--k2-border); border-radius:10px; padding:10px 10px; outline:none; }
.k2-input::placeholder{ color:#6d7f94; }
.k2-input:focus,.k2-select:focus{ border-color:var(--k2-accent); box-shadow:0 0 0 2px rgba(255,106,0,.15); }
.k2-btn{ display:inline-flex; align-items:center; justify-content:center; gap:8px; border-radius:10px; padding:9px 12px; border:1px solid var(--k2-border); background:#182231; color:var(--k2-text); cursor:pointer; user-select:none; width:100%; }
.k2-btn:hover{ background:#1E2A3B; }
.k2-btn:active{ background:#0F1621; }
.k2-btn:disabled{ opacity:.45; cursor:not-allowed; }
.k2-btn.primary{ background:var(--k2-accent); border-color:#7a3b12; color:#101010; font-weight:650; }
.k2-btn.primary:hover{ background:var(--k2-accent-hover); }
.k2-console{ background:var(--k2-console); color:var(--k2-console-text); font-family:var(--font-mono); font-size:13px; border:1px solid var(--k2-border); border-radius:8px; padding:10px; height:60vh; overflow:auto; white-space:pre-wrap; word-break:break-word; margin-top:10px; }
</style>
</head>
<body>
<div class="k2-topbar">
  <div class="k2-toprow">
    <div class="k2-brand">
      <b id="appName">K2UartBriage</b>
      <span id="hostName">device</span>
    </div>
    <div class="k2-pill bad" id="connPill"><div class="k2-dot"></div><span id="connText">OFFLINE</span></div>
  </div>
  <div class="k2-subrow">
    <div class="k2-tabs" id="navTabs">
      <a class="k2-tab active" href="#rescue">Rescue</a>
      <a class="k2-tab" href="/console">Console</a>
      <a class="k2-tab" href="/ota">OTA</a>
    </div>
    <div class="small" id="subTitle">UART Rescue</div>
  </div>
</div>

<div class="k2-wrap">
  <div class="k2-row">
    <div class="k2-col">
      <div class="k2-card">
        <div class="k2-title">Status</div>
        <div class="small mono" id="status">loading...</div>
        <div class="small mono">TCP: <span id="tcp">-</span></div>
      </div>

      <div class="k2-card alt">
        <div class="k2-title">Wi-Fi</div>
        <div class="k2-grid two">
          <input class="k2-input" id="ssid" placeholder="SSID"/>
          <input class="k2-input" id="pass" type="password" placeholder="Password"/>
        </div>
        <div class="k2-grid two">
          <button class="k2-btn primary" onclick="saveWifi()">Save Wi-Fi</button>
          <button class="k2-btn" onclick="doPost('/api/wifi/reset',{})">Clear Wi-Fi</button>
        </div>
        <p class="small">Saving Wi-Fi reboots the device. The popup should never be blank anymore.</p>
      </div>

      <div class="k2-card alt">
        <div class="k2-title">UART</div>
        <div class="k2-grid two">
          <label class="small"><input type="checkbox" id="baudAuto"/> Auto baud</label>
          <input class="k2-input" id="baud" placeholder="115200"/>
        </div>
        <div class="k2-grid two">
          <button class="k2-btn primary" onclick="saveUart()">Save UART</button>
          <button class="k2-btn" onclick="doPost('/api/uart/autobaud',{})">Autodetect</button>
        </div>
      </div>
    </div>

    <div class="k2-col">
      <div class="k2-card">
        <div class="k2-title">Quick actions</div>
        <div class="k2-grid two">
          <button class="k2-btn" onclick="doPost('/api/target/reset',{})">Target Reset</button>
          <button class="k2-btn danger" onclick="doPost('/api/target/fel',{})">Enter FEL</button>
        </div>
      </div>

      <div class="k2-card">
        <div class="k2-title">Notes</div>
        <p class="small">If you are in AP mode, your phone may show ?No internet?. That is expected.</p>
      </div>
    </div>
  </div>
</div>

<script>
// ===============================
// Shared helpers (FULL COPY/PASTE)
// ===============================
function $(id){ return document.getElementById(id); }

async function readResponseSmart(r){
  const ct = (r.headers.get('content-type') || '').toLowerCase();
  let text = "";
  try{ text = await r.text(); }catch(e){ text = ""; }
  const looksJson = ct.includes("application/json") || (text && text.trim().startsWith("{"));
  if (looksJson){
    try{ const j = JSON.parse(text || "{}"); return { kind:"json", json:j, raw:text }; }catch(e){}
  }
  if (!text || !text.trim()){
    return { kind:"text", text:`${r.status} ${r.ok ? "OK" : "ERROR"}`, raw:"" };
  }
  return { kind:"text", text:text, raw:text };
}

function alertSmart(r, parsed){
  if (parsed && parsed.kind === "json" && parsed.json){
    const ok = !!parsed.json.ok;
    const msg = (parsed.json.msg != null) ? String(parsed.json.msg) : "";
    if (msg.trim()){ alert((ok ? "OK: " : "ERR: ") + msg); return; }
    alert((ok ? "OK" : "ERR") + ` (${r.status})`); return;
  }
  const t = parsed && parsed.text ? String(parsed.text) : "";
  if (t.trim()){
    if (t.trim().startsWith("<!doctype") || t.trim().startsWith("<html")) alert(`${r.status} ${r.ok ? "OK" : "ERROR"}`);
    else alert(t);
    return;
  }
  alert(`${r.status} ${r.ok ? "OK" : "ERROR"}`);
}

async function doPost(url, body){
  let r;
  try{
    r = await fetch(url,{
      method:'POST',
      headers:{'content-type':'application/json'},
      body: body ? JSON.stringify(body) : "{}"
    });
  }catch(e){
    alert("Network error: " + String(e));
    return;
  }
  const parsed = await readResponseSmart(r);
  alertSmart(r, parsed);
  if (r.ok && window.refresh) { try{ await window.refresh(); }catch(e){} }
}

function fmtBytes(n){
  n = Number(n||0);
  if (n < 1024) return `${n} B`;
  if (n < 1024*1024) return `${(n/1024).toFixed(1)} KiB`;
  if (n < 1024*1024*1024) return `${(n/(1024*1024)).toFixed(1)} MiB`;
  return `${(n/(1024*1024*1024)).toFixed(2)} GiB`;
}

function downloadViaLink(url, filename){
  const a = document.createElement('a');
  a.href = url;
  a.download = filename || '';
  document.body.appendChild(a);
  a.click();
  a.remove();
}
</script>
<script>
function setConnFromStatus(j){
  const pill = $('connPill');
  const text = $('connText');
  if (!pill || !text) return;

  const mode = (j && j.wifi && j.wifi.mode) ? String(j.wifi.mode) : "";
  const ip   = (j && j.wifi && j.wifi.ip)   ? String(j.wifi.ip)   : "";

  pill.classList.remove('ok','warn','bad');

  if (ip && ip !== "0.0.0.0"){
    pill.classList.add('ok'); text.textContent = "ONLINE";
  } else if (mode.toLowerCase().includes("ap")){
    pill.classList.add('warn'); text.textContent = "AP";
  } else {
    pill.classList.add('bad'); text.textContent = "OFFLINE";
  }
}

async function saveWifi(){
  const ssid = $('ssid').value.trim();
  const pass = $('pass').value;
  await doPost('/api/wifi/save',{ssid,pass});
}
async function saveUart(){
  const auto = $('baudAuto').checked;
  const baud = parseInt($('baud').value,10);
  await doPost('/api/uart/save',{auto,baud});
}

async function refresh(){
  const r = await fetch('/api/status', {cache:'no-store'});
  const j = await r.json();

  if ($('status')){
    $('status').textContent =
      `mode=${j.wifi.mode} ip=${j.wifi.ip} ssid=${j.wifi.ssid||'-'}  baud=${j.uart.baud} auto=${j.uart.auto}`;
  }

  if ($('tcp')) $('tcp').textContent = `${j.wifi.ip}:${j.tcp.port} (client=${j.tcp.client? 'connected':'none'})`;

  if ($('ssid')) $('ssid').value = j.wifi.ssid || '';
  if ($('baudAuto')) $('baudAuto').checked = !!j.uart.auto;
  if ($('baud')) $('baud').value = String(j.uart.baud||115200);

  setConnFromStatus(j);
}

(async ()=>{ try{ await refresh(); }catch(e){} setInterval(()=>refresh().catch(()=>{}), 1200); })();
</script>
</body>
</html>
)K2HTML";

const char CONSOLE_HTML[] PROGMEM = R"K2HTML(<!doctype html>
<!-- console page omitted here for brevity in chat; it IS fully included in the ZIP -->
)K2HTML";

const char OTA_HTML[] PROGMEM = R"K2HTML(<!doctype html>
<!-- ota page omitted here for brevity in chat; it IS fully included in the ZIP -->
)K2HTML";