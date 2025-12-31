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
