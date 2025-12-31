#include "K2BUI.h"
#include <ArduinoJson.h>
#include <cstring>   // memcpy

AsyncWebSocket* K2BUI::_ws = nullptr;
K2BUI::Callbacks K2BUI::_cb = {};
const char* K2BUI::WS_PATH = "/_sys/ws";

// ============================================================
// Simple auth table for 1..N clients (ESP32 friendly).
// Uses client->id() (no getUserData/setUserData dependency)
// ============================================================
static const int MAX_AUTH = 8;
static uint32_t gAuthIds[MAX_AUTH] = {0};
static bool     gAuthOk[MAX_AUTH]  = {false};

static int findSlot(uint32_t id) {
  for (int i = 0; i < MAX_AUTH; ++i) if (gAuthIds[i] == id) return i;
  return -1;
}
static int findFree() {
  for (int i = 0; i < MAX_AUTH; ++i) if (gAuthIds[i] == 0) return i;
  return -1;
}
static void dropSlot(uint32_t id) {
  int s = findSlot(id);
  if (s >= 0) {
    gAuthIds[s] = 0;
    gAuthOk[s]  = false;
  }
}

bool K2BUI::isAuthed(uint32_t id) {
  int s = findSlot(id);
  return (s >= 0) ? gAuthOk[s] : false;
}

void K2BUI::setAuthed(uint32_t id, bool v) {
  int s = findSlot(id);
  if (s < 0) {
    if (!v) return; // fail closed + don't allocate for false
    s = findFree();
    if (s < 0) return; // no space; fail closed
    gAuthIds[s] = id;
  }
  gAuthOk[s] = v;

  // IMPORTANT: reclaim slot immediately when not authed
  if (!v) dropSlot(id);
}

// ============================================================
// Lifecycle
// ============================================================
void K2BUI::begin(AsyncWebServer& server, const Callbacks& cb) {
  _cb = cb;

  // Create WS handler on hidden path
  _ws = new AsyncWebSocket(WS_PATH);
  _ws->onEvent(&K2BUI::onWsEvent);

  server.addHandler(_ws);

  // Optional: tiny endpoint that proves the WS module exists
  addDebugEndpoints(server);
}

void K2BUI::tick() {
  if (_ws) _ws->cleanupClients();
}

// ============================================================
// UART RX -> WS (binary)
// Most compatible across ESPAsyncWebServer forks:
// uses makeBuffer + binaryAll(buffer)
// ============================================================
void K2BUI::onUartRx(const uint8_t* data, size_t len) {
  if (!_ws || !data || !len) return;

  AsyncWebSocketMessageBuffer* buf = _ws->makeBuffer(len);
  if (!buf) return;

  memcpy(buf->get(), data, len);

  // Broadcast to all clients.
  // NOTE: if you want strict "only authed receive", do that on the client side
  // (ignore binary until auth), OR move to a fork with a stable client-iteration API.
  _ws->binaryAll(buf);
}

// ============================================================
// Debug endpoint
// ============================================================
void K2BUI::addDebugEndpoints(AsyncWebServer& server) {
  server.on("/_sys/ws_info", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", "{\"ok\":true,\"ws\":\"/_sys/ws\"}");
  });
}

// ============================================================
// JSON helpers
// ============================================================
void K2BUI::sendJsonErr(AsyncWebSocketClient* client, const char* code, const char* msg) {
  if (!client) return;
  JsonDocument d;
  d["t"] = "err";
  d["code"] = code;
  d["msg"] = msg;
  String out;
  serializeJson(d, out);
  client->text(out);
}

bool K2BUI::handleText(AsyncWebSocketClient* client, const String& msg) {
  if (!client) return true;

  JsonDocument d;
  DeserializationError e = deserializeJson(d, msg);
  if (e) {
    sendJsonErr(client, "bad_json", "Failed to parse JSON");
    return true;
  }

  const char* t = d["t"] | "";
  if (!t || !t[0]) {
    sendJsonErr(client, "missing_type", "Missing field t");
    return true;
  }

  // ---- AUTH ----
  if (strcmp(t, "auth") == 0) {
    const char* token = d["token"] | "";
    bool ok = false;

    if (_cb.auth_check) {
      ok = _cb.auth_check(String(token));
    } else {
      ok = (String(token) == "CHANGE_ME_TOKEN");
    }

    setAuthed(client->id(), ok);

    JsonDocument r;
    r["t"] = "auth";
    r["ok"] = ok;
    String out;
    serializeJson(r, out);
    client->text(out);

    return true;
  }

  // Allow ping without auth if you want
  if (strcmp(t, "ping") == 0) {
    client->text("{\"t\":\"pong\"}");
    return true;
  }

  // Everything else requires auth
  if (!isAuthed(client->id())) {
    sendJsonErr(client, "unauthorized", "Send {t:'auth',token:'...'} first");
    return true;
  }

  // ---- iCommand ----
  if (strcmp(t, "cmd") == 0) {
    const char* line = d["line"] | "";
    if (!_cb.icommand_exec) {
      sendJsonErr(client, "no_icommand", "icommand_exec callback not set");
      return true;
    }
    String out = _cb.icommand_exec(String(line));

    JsonDocument r;
    r["t"] = "cmd";
    r["ok"] = true;
    r["out"] = out;
    String resp;
    serializeJson(r, resp);
    client->text(resp);
    return true;
  }

  // ---- iBP (structured) ----
  if (strcmp(t, "ibp") == 0) {
    if (!_cb.ibp_exec) {
      sendJsonErr(client, "no_ibp", "ibp_exec callback not set");
      return true;
    }
    String resp = _cb.ibp_exec(msg);
    client->text(resp);
    return true;
  }

  // ---- UART TX (text) ----
  if (strcmp(t, "uart_tx") == 0) {
    const char* s = d["data"] | "";
    if (!_cb.uart_write) {
      sendJsonErr(client, "no_uart", "uart_write callback not set");
      return true;
    }
    _cb.uart_write((const uint8_t*)s, strlen(s));
    client->text("{\"t\":\"uart_tx\",\"ok\":true}");
    return true;
  }

  sendJsonErr(client, "unknown_type", t);
  return true;
}

// ============================================================
// WS event dispatcher
// ============================================================
void K2BUI::onWsEvent(
  AsyncWebSocket* server,
  AsyncWebSocketClient* client,
  AwsEventType type,
  void* arg,
  uint8_t* data,
  size_t len
) {
  (void)server;

  if (!client) return;

  if (type == WS_EVT_CONNECT) {
    client->text("{\"t\":\"hello\",\"v\":1}");
    setAuthed(client->id(), false);
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    dropSlot(client->id());
    return;
  }

  if (type != WS_EVT_DATA) return;

  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!info) return;

  // TEXT: JSON protocol
  if (info->opcode == WS_TEXT) {
    String msg;
    msg.reserve(len + 1);
    for (size_t i = 0; i < len; ++i) msg += (char)data[i];
    handleText(client, msg);
    return;
  }

  // BINARY: raw UART TX fast-path
  if (info->opcode == WS_BINARY) {
    if (!isAuthed(client->id())) {
      sendJsonErr(client, "unauthorized", "Binary requires auth");
      return;
    }
    if (_cb.uart_write) {
      _cb.uart_write(data, len);
    } else {
      sendJsonErr(client, "no_uart", "uart_write callback not set");
    }
    return;
  }
}