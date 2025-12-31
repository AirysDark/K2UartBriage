// K2UartBriage WebUI script (extracted from PROGMEM pages to make LittleFS UI functional)
// NOTE: Kept minimal to match existing backend endpoints.
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

// ===============================
// Console / status helpers
// ===============================

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

  // Header app info (always visible)
  if ($('appName') && j.app && j.app.name) $('appName').textContent = String(j.app.name);
  if ($('appVer') && j.app && j.app.version) $('appVer').textContent = `v${j.app.version}`;

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


// ===============================
// Header models / navigation
// ===============================

// Keep backward compatibility with older pages that call this.
async function loadHeaderModels(){
  // In current LittleFS pages, nav tabs are static.
  // This is a no-op placeholder to avoid runtime errors.
  return true;
}

// ===============================
// UART Console Page (WebSocket /ws)
// ===============================

function _b64FromArrayBuffer(buf){
  const bytes = new Uint8Array(buf);
  let bin = "";
  const chunk = 0x8000;
  for (let i=0;i<bytes.length;i+=chunk){
    bin += String.fromCharCode.apply(null, bytes.subarray(i, i+chunk));
  }
  return btoa(bin);
}

function _setWsPill(connected){
  const st = $('st');
  if (!st) return;
  if (connected){
    st.classList.remove('bad');
    st.classList.add('ok');
    st.innerHTML = '<div class="k2-dot"></div><span>WS: connected</span>';
  } else {
    st.classList.remove('ok');
    st.classList.add('bad');
    st.innerHTML = '<div class="k2-dot"></div><span>WS: disconnected</span>';
  }
}

function _appendConsole(text){
  const out = $('out');
  if (!out) return;
  // preserve newlines
  const atBottom = (out.scrollTop + out.clientHeight + 5) >= out.scrollHeight;
  const pre = document.createElement('pre');
  pre.className = 'mono';
  pre.style.margin = '0';
  pre.style.whiteSpace = 'pre-wrap';
  pre.textContent = text;
  out.appendChild(pre);
  if (atBottom) out.scrollTop = out.scrollHeight;
}

// WebUI console uses the built-in K2BUI hidden WS (/_sys/ws).
// This is separate from the public /ws CK2-auth websocket used by external tools.
// K2BUI expects a small JSON protocol.
function _k2buiAuth(ws){
  // Default token is CHANGE_ME_TOKEN (unless you later store a prefs wsToken).
  // WebUI should not require the CK2 file.
  const msg = { t:"auth", token:"CHANGE_ME_TOKEN" };
  ws.send(JSON.stringify(msg));
}

function initConsolePage(){
  // Prevent crashes if page not present
  const input = $('in');
  const hostName = $('hostName');
  if (hostName) hostName.textContent = location.host;

  // If the page didn't include required elements, bail safely.
  if (!input || !$('out')) return;

  // Connect WS (K2BUI hidden console)
  let proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
  let wsUrl = proto + location.host + '/_sys/ws';
  let ws = null;

  function connect(){
    try{
      ws = new WebSocket(wsUrl);
    }catch(e){
      _appendConsole('[UI] WebSocket create failed: ' + (e && e.message ? e.message : e) + '\n');
      _setWsPill(false);
      return;
    }

    ws.onopen = () => {
      _setWsPill(true);
      _appendConsole('[UI] WS opened\n');
      _k2buiAuth(ws); // auth with built-in token
    };

    ws.onmessage = (ev) => {
      // TEXT: JSON messages; BINARY: UART RX bytes
      if (typeof ev.data === 'string') {
        try{
          const j = JSON.parse(ev.data);
          // Common K2BUI messages: hello/auth/cmd/err/uart_tx
          if (j && j.t === 'auth') {
            _appendConsole(`[K2BUI] auth ${j.ok ? 'OK' : 'FAIL'}\n`);
            return;
          }
          if (j && j.t === 'cmd') {
            if (j.out) _appendConsole(String(j.out) + '\n');
            else _appendConsole('[K2BUI] cmd ok\n');
            return;
          }
          if (j && j.t === 'err') {
            _appendConsole(`[K2BUI] ERR ${j.code||''}: ${j.msg||''}\n`);
            return;
          }
        }catch(e){
          // fall through: show raw text
        }
        _appendConsole(ev.data);
        return;
      }

      // Blob/ArrayBuffer -> text best-effort
      const handleBuf = (buf) => {
        try{
          const td = new TextDecoder('utf-8');
          const s = td.decode(buf);
          if (s) _appendConsole(s);
        }catch(e){
          _appendConsole('[K2BUI] (binary rx)\n');
        }
      };

      if (ev.data instanceof Blob) {
        ev.data.arrayBuffer().then(handleBuf);
      } else if (ev.data instanceof ArrayBuffer) {
        handleBuf(ev.data);
      }
    };

    ws.onclose = () => {
      _setWsPill(false);
      _appendConsole('[UI] WS closed\n');
      // retry after a moment
      setTimeout(connect, 1000);
    };

    ws.onerror = () => {
      // browser will also trigger onclose
      _setWsPill(false);
    };
  }

  connect();

  function sendLine(){
    const v = input.value || '';
    if (!v.trim()) return;
    const line = v.endsWith('\n') ? v : (v + '\n');
    if (ws && ws.readyState === WebSocket.OPEN){
      // Route through K2BUI command channel so your existing command router handles ihelp/ibp/etc.
      ws.send(JSON.stringify({ t:"cmd", line }));
    } else {
      _appendConsole('[UI] WS not connected\n');
    }
    input.value = '';
  }

  input.addEventListener('keydown', (e)=>{
    if (e.key === 'Enter'){
      e.preventDefault();
      sendLine();
    }
  });

  // Some pages include a Send button; hook it if present.
  // (button may not have an id; try common selectors)
  const btns = document.querySelectorAll('button');
  for (const b of btns){
    if ((b.textContent || '').trim().toLowerCase() === 'send'){
      b.addEventListener('click', (e)=>{ e.preventDefault(); sendLine(); });
    }
  }
}

// ===============================
// OTA Page (LittleFS: /www/ota.html)
// Endpoint: POST /api/ota/upload (multipart)
// ===============================

function _otaLog(msg){
  const el = $('log');
  if (!el) return;
  el.textContent += (String(msg).endsWith('\n') ? String(msg) : (String(msg) + "\n"));
  el.scrollTop = el.scrollHeight;
}

function _otaSetPct(p){
  p = Math.max(0, Math.min(100, Math.floor(Number(p)||0)));
  const fill = $('fill');
  const pct = $('pct');
  if (fill) fill.style.width = p + '%';
  if (pct) pct.textContent = p + '%';
}

function initOtaPage(){
  // Host label
  if ($('hostName')) $('hostName').textContent = location.host;

  // Status pill (lightweight)
  const st = $('st');
  if (st){
    st.classList.remove('bad');
    st.classList.add('ok');
    st.innerHTML = '<div class="k2-dot"></div><span>ready</span>';
  }

  // Reset
  _otaSetPct(0);
  const log = $('log');
  if (log) log.textContent = '';

  // Optional: show current build info if available
  fetch('/api/status', {cache:'no-store'})
    .then(r=>r.json())
    .then(j=>{
      _otaLog(`[status] ip=${j?.wifi?.ip || '-'} ssid=${j?.wifi?.ssid || '-'}\n`);
    })
    .catch(()=>{});
}

// ============================================
// Online update (GitHub Releases)
// ============================================
let _ghChecked = null; // {tag,size}
let _ghPollTimer = null;

function _ghSetLatest(text){
  const el = $('ghLatest');
  if (el) el.textContent = text;
}

async function checkGithubUpdate(){
  _ghSetLatest('checking...');
  _otaLog('[github] checking latest release...\n');
  try{
    const r = await fetch('/api/ota/github_check', {cache:'no-store'});
    const j = await r.json();
    if (!r.ok || !j.ok){
      _ghChecked = null;
      const msg = (j && j.msg) ? j.msg : ('HTTP ' + r.status);
      _ghSetLatest('error');
      _otaLog('[github] check failed: ' + msg + '\n');
      return;
    }
    _ghChecked = { tag: j.tag || 'latest', size: j.size || 0 };
    _ghSetLatest((_ghChecked.tag) + ' (' + (_ghChecked.size||0) + ' bytes)');
    _otaLog('[github] latest: ' + _ghChecked.tag + '\n');
  }catch(e){
    _ghChecked = null;
    _ghSetLatest('error');
    _otaLog('[github] check error: ' + (e?.message || e) + '\n');
  }
}

async function confirmGithubUpdate(){
  if (!_ghChecked){
    await checkGithubUpdate();
    if (!_ghChecked) return;
  }
  const ok = confirm('This will update firmware AND LittleFS from GitHub. Continue?');
  if (!ok) return;
  startGithubUpdate();
}

async function startGithubUpdate(){
  _otaSetPct(0);
  _otaLog('[github] starting online update...\n');
  try{
    const r = await fetch('/api/ota/github_update', {method:'POST'});
    const t = await r.text();
    if (!r.ok){
      _otaLog('[github] start failed: ' + t + '\n');
      return;
    }
    _otaLog('[github] ' + t + '\n');
    // Poll progress
    if (_ghPollTimer) clearInterval(_ghPollTimer);
    _ghPollTimer = setInterval(pollGithubProgress, 700);
  }catch(e){
    _otaLog('[github] start error: ' + (e?.message || e) + '\n');
  }
}

async function pollGithubProgress(){
  try{
    const r = await fetch('/api/ota/github_progress', {cache:'no-store'});
    const j = await r.json();
    const pct = (j && typeof j.pct === 'number') ? j.pct : 0;
    _otaSetPct(pct);
    const phase = j?.phase || '';
    const msg = j?.msg || '';
    if (phase){
      _otaLog('[github] phase=' + phase + (msg ? (', msg=' + msg) : '') + '\n');
    }
    if (!j?.active && (phase === 'done' || phase === 'error')){
      clearInterval(_ghPollTimer);
      _ghPollTimer = null;
    }
  }catch(_e){
    // ignore intermittent poll errors
  }
}

async function doUpload() {
  const f = $('fw');
  const file = f && f.files && f.files[0];
  
  if (file && (file.name||"").toLowerCase() === "update.zip") {
    // Resumable staged upload for dual-image container
    otaSessionUpload(file).catch(e=>otaLog(`[error] ${e.message||e}`));
    return;
  }
if (!file){
    alert('Pick an update.zip (or .bin) file first');
    return;
  }

  const name = String(file.name||'').toLowerCase();
  const isZip = name.endsWith('.zip');
  const isBin = name.endsWith('.bin');
  if (!isZip && !isBin){
    alert('Please select update.zip (preferred) or a firmware .bin');
    return;
  }

  const url = isZip ? '/api/ota/updatezip' : '/api/ota/upload';
  if (isZip) {
    _otaLog('[info] update.zip uses a streamed dual-image container: firmware + littlefs');
  }

  const btn = $('btn');
  if (btn) btn.disabled = true;
  _otaLog(`[upload] ${file.name} (${fmtBytes(file.size)})`);
  _otaSetPct(0);

  // Use XHR for upload progress
  const form = new FormData();
  form.append('file', file, file.name);

  await new Promise((resolve)=>{
    const xhr = new XMLHttpRequest();
    xhr.open('POST', url);

    xhr.upload.onprogress = (e)=>{
      if (e.lengthComputable){
        const p = (e.loaded / e.total) * 100;
        _otaSetPct(p);
      }
    };

    xhr.onerror = ()=>{
      _otaLog('[error] network/upload failed');
      resolve();
    };

    xhr.onload = ()=>{
      _otaLog(`[resp] HTTP ${xhr.status}`);
      if (xhr.responseText) _otaLog(xhr.responseText);
      // If success, device will reboot shortly.
      resolve();
    };

    xhr.send(form);
  });

  if (btn) btn.disabled = false;
}


// ===============================
// OTA: resumable chunked upload (update.zip container)
// ===============================
async function otaSessionUpload(file) {
  const log = (m)=> otaLog(m);
  const CHUNK = 65536;

  log(`[sess] starting session (size=${file.size})...`);
  let resp = await fetch(`/api/ota/session/start`, {
    method: "POST",
    headers: {"Content-Type":"application/x-www-form-urlencoded"},
    body: `size=${encodeURIComponent(file.size)}`
  });
  if (!resp.ok) {
    log(`[sess] start failed HTTP ${resp.status}`);
    throw new Error("session start failed");
  }
  const s = await resp.json();
  log(`[sess] id=${s.id} have=${s.have}/${s.total}`);

  let offset = s.have || 0;

  while (offset < file.size) {
    const slice = file.slice(offset, offset + CHUNK);
    const buf = await slice.arrayBuffer();

    try {
      const up = await fetch(`/api/ota/session/chunk?id=${encodeURIComponent(s.id)}&offset=${offset}`, {
        method: "POST",
        headers: {"Content-Type":"application/octet-stream"},
        body: buf
      });

      if (!up.ok) {
        log(`[sess] chunk HTTP ${up.status}, retrying...`);
        await sleep(3000);
        const st = await fetch(`/api/ota/session/status`).then(r=>r.json()).catch(()=>null);
        if (st && st.have != null) offset = st.have;
        continue;
      }

      offset += buf.byteLength;
      otaSetPct(Math.floor((offset * 100) / file.size));
    } catch (e) {
      log(`[sess] network error, waiting...`);
      await sleep(3000);
      const st = await fetch(`/api/ota/session/status`).then(r=>r.json()).catch(()=>null);
      if (st && st.have != null) offset = st.have;
    }
  }

  log(`[sess] finalize...`);
  const fin = await fetch(`/api/ota/session/finalize`, {method:"POST"});
  const txt = await fin.text();
  log(`[sess] ${txt}`);
  if (!fin.ok) throw new Error("finalize failed");
}

function sleep(ms){ return new Promise(r=>setTimeout(r, ms)); }
