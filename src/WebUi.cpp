// ===============================
// src/WebUi.cpp  (FULL COPY/PASTE)
// ===============================
#include "WebUi.h"
#include "Web_pages.h"     // INDEX_HTML / CONSOLE_HTML (PROGMEM) live here
#include "AppConfig.h"
#include "Debug.h"
#include "Storage.h"
#include "UartBridge.h"
#include "OTA.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ===== CK2 deps =====
#include <LittleFS.h>
#include <Preferences.h>
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

// ---- needed for CK2 vectors/time ----
#include <vector>
#include <time.h>



DBG_REGISTER_MODULE(__FILE__);

// Use AppConfig ports (single source of truth)
static AsyncWebServer web(CFG_WEB_PORT);
static AsyncWebSocket ws("/ws");

static void sendJson(AsyncWebServerRequest* req, int code, bool ok, const String& msg, JsonDocument* extra = nullptr) {
  JsonDocument d;
  d["ok"] = ok;
  d["msg"] = msg;
  if (extra) {
    // merge "extra" into d under "data"
    JsonVariant data = d["data"].to<JsonVariant>();
    data.set((*extra).as<JsonVariant>());
  }
  String out;
  serializeJson(d, out);
  req->send(code, "application/json", out);
}

void WebUi::bootBanner() {
  printBootBanner("WEB", "HTTP + WebSocket + captive redirects");
}

static void wsBroadcastImpl(const uint8_t* data, size_t len) {
  String s; s.reserve(len);
  for (size_t i = 0; i < len; i++) s += (char)data[i];
  ws.textAll(s);
}

bool WebUi::isCaptiveRequest(BridgeState& st, AsyncWebServerRequest* req) {
  if (!st.apMode) return false;
  if (!req->hasHeader("Host")) return false;
  String host = req->header("Host");
  if (host.indexOf(AP_IP.toString()) >= 0) return false;
  return true;
}

// ============================================================
// CK2 - AES-256-GCM encrypted keyfile stored in LittleFS
// Endpoints:
//   POST /api/ck2/generate?ttl=604800  -> downloads CK2.key (octet-stream)
//   GET  /api/ck2/download            -> downloads last saved CK2.key
//   GET  /api/ck2/verify_last         -> debug verify json
// ============================================================

static const char* CK2_FS_DIR    = "/ck2";
static const char* CK2_LAST_PATH = "/ck2/last.ck2";
static const char* CK2_PREF_NS   = "bridge";
static const char* CK2_PREF_KEY  = "ck2_master";

#pragma pack(push, 1)
struct CK2Header {
  char     magic[4];     // "CK2\1"
  uint8_t  version;      // 1
  uint8_t  alg;          // 1 = AES-256-GCM
  uint16_t reserved;     // 0
  uint32_t payload_len;  // plaintext length
  uint32_t iat;          // issued-at (unix-ish)
  uint32_t exp;          // expiry (unix-ish)
  uint8_t  nonce[12];    // GCM nonce
  // followed by: ciphertext[payload_len] + tag[16]
};
#pragma pack(pop)

static void ck2RandomBytes(uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; i += 4) {
    uint32_t r = esp_random();
    size_t left = n - i;
    size_t take = (left >= 4) ? 4 : left;
    memcpy(out + i, &r, take);
  }
}

static bool ck2LoadOrCreateMaster(uint8_t out32[32]) {
  Preferences p;
  if (!p.begin(CK2_PREF_NS, false)) return false;

  size_t len = p.getBytesLength(CK2_PREF_KEY);
  if (len == 32) {
    p.getBytes(CK2_PREF_KEY, out32, 32);
    p.end();
    return true;
  }

  ck2RandomBytes(out32, 32);
  size_t w = p.putBytes(CK2_PREF_KEY, out32, 32);
  p.end();
  return (w == 32);
}

// Derive AES key from master + context using SHA256(master || context)
static void ck2DeriveKey(const uint8_t master32[32], const char* context, uint8_t out32[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, master32, 32);
  mbedtls_sha256_update_ret(&ctx, (const unsigned char*)context, strlen(context));
  mbedtls_sha256_finish_ret(&ctx, out32);
  mbedtls_sha256_free(&ctx);
}

static uint32_t ck2ChipId32() {
  uint64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac ^ (mac >> 32));
}

static uint32_t ck2NowUnixish() {
  uint32_t now = (uint32_t)(time(nullptr));
  if (now < 100000U) {
    // fallback if RTC/NTP not set
    now = (uint32_t)(millis() / 1000UL) + 1700000000UL;
  }
  return now;
}

static bool ck2AesGcmEncrypt(
  const uint8_t key32[32],
  const uint8_t nonce12[12],
  const uint8_t* plain, size_t plainLen,
  uint8_t* cipherOut,
  uint8_t tag16[16]
) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key32, 256) != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  const char* aad = "CK2-AAD-v1";
  int rc = mbedtls_gcm_crypt_and_tag(
    &gcm, MBEDTLS_GCM_ENCRYPT,
    plainLen,
    nonce12, 12,
    (const unsigned char*)aad, strlen(aad),
    plain,
    cipherOut,
    16, tag16
  );

  mbedtls_gcm_free(&gcm);
  return (rc == 0);
}

static bool ck2AesGcmDecrypt(
  const uint8_t key32[32],
  const uint8_t nonce12[12],
  const uint8_t* cipher, size_t cipherLen,
  const uint8_t tag16[16],
  uint8_t* plainOut
) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key32, 256) != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  const char* aad = "CK2-AAD-v1";
  int rc = mbedtls_gcm_auth_decrypt(
    &gcm,
    cipherLen,
    nonce12, 12,
    (const unsigned char*)aad, strlen(aad),
    tag16, 16,
    cipher,
    plainOut
  );

  mbedtls_gcm_free(&gcm);
  return (rc == 0);
}

static bool ck2GenerateFile(std::vector<uint8_t>& outFile, uint32_t ttlSeconds) {
  uint8_t master[32];
  if (!ck2LoadOrCreateMaster(master)) return false;

  uint8_t key[32];
  ck2DeriveKey(master, "CK2-K2UartBriage-v1", key);

  const uint32_t iat = ck2NowUnixish();
  const uint32_t exp = iat + ttlSeconds;

  JsonDocument d;
  d["dev"]  = ck2ChipId32();
  d["iat"]  = iat;
  d["exp"]  = exp;
  d["perm"] = "ws";

  uint8_t sidRaw[8];
  ck2RandomBytes(sidRaw, sizeof(sidRaw));
  char sidHex[17]; sidHex[16] = 0;
  for (int i=0;i<8;i++) sprintf(&sidHex[i*2], "%02X", sidRaw[i]);
  d["sid"] = sidHex;

  String payload;
  serializeJson(d, payload);

  CK2Header h{};
  h.magic[0] = 'C'; h.magic[1] = 'K'; h.magic[2] = '2'; h.magic[3] = 0x01;
  h.version = 1;
  h.alg = 1;
  h.reserved = 0;
  h.payload_len = (uint32_t)payload.length();
  h.iat = iat;
  h.exp = exp;
  ck2RandomBytes(h.nonce, sizeof(h.nonce));

  const size_t cipherLen = h.payload_len;
  const size_t total = sizeof(CK2Header) + cipherLen + 16;

  outFile.clear();
  outFile.resize(total);

  memcpy(outFile.data(), &h, sizeof(CK2Header));

  uint8_t* cipherPtr = outFile.data() + sizeof(CK2Header);
  uint8_t* tagPtr    = cipherPtr + cipherLen;

  uint8_t tag16[16];
  if (!ck2AesGcmEncrypt(key, h.nonce,
                       (const uint8_t*)payload.c_str(), payload.length(),
                       cipherPtr, tag16)) {
    return false;
  }
  memcpy(tagPtr, tag16, 16);

  return true;
}

static bool ck2VerifyAndExtract(const uint8_t* fileData, size_t fileLen, String& outJson, String& outErr) {
  outErr = "";

  if (!fileData || fileLen < sizeof(CK2Header) + 16) {
    outErr = "too_small";
    return false;
  }

  CK2Header h{};
  memcpy(&h, fileData, sizeof(CK2Header));

  if (!(h.magic[0]=='C' && h.magic[1]=='K' && h.magic[2]=='2' && h.magic[3]==0x01)) {
    outErr = "bad_magic";
    return false;
  }
  if (h.version != 1 || h.alg != 1) {
    outErr = "unsupported";
    return false;
  }

  const size_t cipherLen = (size_t)h.payload_len;
  const size_t need = sizeof(CK2Header) + cipherLen + 16;
  if (fileLen < need) {
    outErr = "truncated";
    return false;
  }

  uint8_t master[32];
  if (!ck2LoadOrCreateMaster(master)) {
    outErr = "no_master";
    return false;
  }

  uint8_t key[32];
  ck2DeriveKey(master, "CK2-K2UartBriage-v1", key);

  const uint8_t* cipherPtr = fileData + sizeof(CK2Header);
  const uint8_t* tagPtr    = cipherPtr + cipherLen;

  std::vector<uint8_t> plain;
  plain.resize(cipherLen + 1);

  if (!ck2AesGcmDecrypt(key, h.nonce, cipherPtr, cipherLen, tagPtr, plain.data())) {
    outErr = "decrypt_fail";
    return false;
  }

  plain[cipherLen] = 0;
  outJson = (const char*)plain.data();

  JsonDocument d;
  if (deserializeJson(d, outJson)) {
    outErr = "json_bad";
    return false;
  }

  uint32_t dev = d["dev"] | 0U;
  uint32_t exp = d["exp"] | 0U;
  if (dev != ck2ChipId32()) {
    outErr = "device_mismatch";
    return false;
  }

  uint32_t now = ck2NowUnixish();
  if (exp && now > exp) {
    outErr = "expired";
    return false;
  }

  return true;
}

static bool ck2EnsureFs() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(CK2_FS_DIR)) LittleFS.mkdir(CK2_FS_DIR);
  return true;
}

static void ck2AttachRoutes() {
  // POST /api/ck2/generate?ttl=604800
  web.on("/api/ck2/generate", HTTP_POST, [](AsyncWebServerRequest* req){
    uint32_t ttl = 7UL * 24UL * 3600UL;
    if (req->hasParam("ttl")) {
      ttl = (uint32_t)req->getParam("ttl")->value().toInt();
      if (ttl < 60) ttl = 60;
      if (ttl > 30UL * 24UL * 3600UL) ttl = 30UL * 24UL * 3600UL;
    }

    if (!ck2EnsureFs()) {
      req->send(500, "text/plain", "LittleFS not mounted");
      return;
    }

    std::vector<uint8_t> ck2;
    if (!ck2GenerateFile(ck2, ttl)) {
      req->send(500, "text/plain", "CK2 generate failed");
      return;
    }

    // Save last.ck2
    File f = LittleFS.open(CK2_LAST_PATH, "w");
    if (!f) {
      req->send(500, "text/plain", "CK2 save failed");
      return;
    }
    size_t w = f.write(ck2.data(), ck2.size());
    f.close();
    if (w != ck2.size()) {
      req->send(500, "text/plain", "CK2 save incomplete");
      return;
    }

    // Stream binary to browser as a download
    AsyncResponseStream* res = req->beginResponseStream("application/octet-stream");
    res->addHeader("Content-Disposition", "attachment; filename=CK2.key");
    res->addHeader("Cache-Control", "no-cache");
    res->write(ck2.data(), ck2.size());
    req->send(res);
  });

  // GET /api/ck2/download
  web.on("/api/ck2/download", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!ck2EnsureFs()) { req->send(500, "text/plain", "LittleFS not mounted"); return; }
    if (!LittleFS.exists(CK2_LAST_PATH)) { req->send(404, "text/plain", "No CK2 generated"); return; }

    File f = LittleFS.open(CK2_LAST_PATH, "r");
    if (!f) { req->send(500, "text/plain", "Open failed"); return; }

    std::vector<uint8_t> buf;
    buf.resize((size_t)f.size());
    f.read(buf.data(), buf.size());
    f.close();

    AsyncResponseStream* res = req->beginResponseStream("application/octet-stream");
    res->addHeader("Content-Disposition", "attachment; filename=CK2.key");
    res->addHeader("Cache-Control", "no-cache");
    res->write(buf.data(), buf.size());
    req->send(res);
  });

  // GET /api/ck2/verify_last (debug)
  web.on("/api/ck2/verify_last", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!ck2EnsureFs()) { req->send(500, "text/plain", "LittleFS not mounted"); return; }
    if (!LittleFS.exists(CK2_LAST_PATH)) { req->send(404, "text/plain", "No CK2 generated"); return; }

    File f = LittleFS.open(CK2_LAST_PATH, "r");
    if (!f) { req->send(500, "text/plain", "Open failed"); return; }

    std::vector<uint8_t> buf;
    buf.resize((size_t)f.size());
    f.read(buf.data(), buf.size());
    f.close();

    String json, err;
    bool ok = ck2VerifyAndExtract(buf.data(), buf.size(), json, err);

    JsonDocument d;
    d["ok"] = ok;
    d["err"] = err;
    d["json"] = ok ? json : "";

    String out;
    serializeJson(d, out);
    req->send(ok ? 200 : 401, "application/json", out);
  });
}

void WebUi::begin(BridgeState& st) {
  st.wsBroadcast = &wsBroadcastImpl;

  ws.onEvent([&st](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      client->text("[WS] connected\n");
      D_WEBLN("ws client connected");
    } else if (type == WS_EVT_DATA) {
      // Only forward WS->UART when enabled
      if (!st.webTxEnabled) return;
      UartBridge::serial().write(data, len);
    }
  });
  web.addHandler(&ws);

  // OTA
  OTA::begin();
  OTA::attach(web);

  // CK2 routes
  ck2AttachRoutes();

  // ----------------------------
  // Pages (PROGMEM)
  // ----------------------------
  web.on("/", HTTP_GET, [&st](AsyncWebServerRequest* req){
    if (WebUi::isCaptiveRequest(st, req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    req->send(LittleFS, "/www/index.html", "text/html");
  });

  web.on("/console", HTTP_GET, [&st](AsyncWebServerRequest* req){
    if (WebUi::isCaptiveRequest(st, req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    req->send(LittleFS, "/www/console.html", "text/html");
  });

  // ----------------------------
  // Status (JSON)
  // ----------------------------
  web.on("/api/status", HTTP_GET, [&st](AsyncWebServerRequest* req){
    JsonDocument d;

    WifiCreds c = Storage::loadWifi();

    JsonObject wifi = d["wifi"].to<JsonObject>();
    wifi["mode"] = st.apMode ? "AP" : "STA";
    wifi["ip"]   = st.apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    wifi["ssid"] = (c.has && c.ssid.length()) ? c.ssid : "";

    JsonObject uart = d["uart"].to<JsonObject>();
    uart["auto"]  = st.baudAuto;
    uart["baud"]  = st.currentBaud;
    uart["webTx"] = st.webTxEnabled;

    JsonObject tcp = d["tcp"].to<JsonObject>();
    tcp["port"]   = CFG_TCP_PORT;
    tcp["client"] = (st.tcpClient != nullptr);

    // AP auto-reset timers (for UI)
    JsonObject ap = d["ap"].to<JsonObject>();
    ap["ap_mode"] = st.apMode;
    ap["timer_armed"] = st.apTimerArmed();
    ap["started_ms"]  = (uint64_t)st.apStartedMs;

    uint64_t elapsed = 0;
    if (st.apStartedMs != 0) elapsed = (uint64_t)(millis() - st.apStartedMs);
    ap["elapsed_ms"] = elapsed;

    ap["auto_reset_enabled"]  = st.noSsidAutoResetEnabled;
    ap["auto_reset_after_ms"] = (uint64_t)st.noSsidAutoResetAfterMs;

    // Useful for UI logic: do we currently have a stored SSID?
    ap["has_saved_ssid"] = (c.has && c.ssid.length() > 0);

    // UI expects these objects to exist -> safe defaults
    JsonObject backup = d["backup"].to<JsonObject>();
    backup["state"] = "idle";
    backup["progress"] = 0;
    backup["profile_id"] = "A";
    backup["custom_start"] = 0;
    backup["custom_count"] = 0;
    backup["latest_ready"] = false;
    backup["have_sd"] = false;
    backup["sd_size"] = 0;
    backup["have_ram"] = false;
    backup["ram_size"] = 0;

    JsonArray profiles = backup["profiles"].to<JsonArray>();
    { JsonObject p = profiles.add<JsonObject>(); p["id"]="A";    p["label"]="Profile A"; }
    { JsonObject p = profiles.add<JsonObject>(); p["id"]="B";    p["label"]="Profile B"; }
    { JsonObject p = profiles.add<JsonObject>(); p["id"]="C";    p["label"]="Profile C"; }
    { JsonObject p = profiles.add<JsonObject>(); p["id"]="FULL"; p["label"]="FULL"; }

    JsonObject restore = d["restore"].to<JsonObject>();
    restore["armed"] = false;
    restore["ready"] = false;
    restore["danger_override"] = false;

    JsonObject uboot = d["uboot"].to<JsonObject>();
    uboot["present"] = false;
    uboot["prompt"] = false;
    uboot["ums_active"] = false;

    JsonObject ota = d["ota"].to<JsonObject>();
    ota["active"] = false;
    ota["written"] = 0;
    ota["total"] = 0;

    String out;
    serializeJson(d, out);
    req->send(200, "application/json", out);
  });

  // ----------------------------
  // Wi-Fi (RETURN JSON, fixes blank popup)
  // ----------------------------
  web.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      JsonDocument d;
      if (deserializeJson(d, data, len)) {
        sendJson(req, 400, false, "Bad JSON");
        return;
      }

      String ssid = d["ssid"] | "";
      String pass = d["pass"] | "";
      if (ssid.length() < 1) {
        sendJson(req, 400, false, "SSID required");
        return;
      }

      Storage::saveWifi(ssid, pass);

      // respond FIRST, then reboot
      sendJson(req, 200, true, "Saved Wi-Fi. Rebooting...");
      delay(250);
      ESP.restart();
    }
  );

  web.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* req){
    Storage::clearWifi();
    sendJson(req, 200, true, "Cleared Wi-Fi creds. Rebooting...");
    delay(250);
    ESP.restart();
  });

  // Manual reset button: reboot immediately
  web.on("/api/wifi/ap_reset_now", HTTP_POST, [](AsyncWebServerRequest* req){
    sendJson(req, 200, true, "Rebooting...");
    delay(250);
    ESP.restart();
  });

  // Toggle/adjust the NO-SSID auto reset timer.
  // Body: {"enabled":true/false, "after_ms":300000}
  web.on("/api/wifi/no_ssid_autoreset", HTTP_POST, [&st](AsyncWebServerRequest* req){}, nullptr,
    [&st](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      JsonDocument d;
      if (deserializeJson(d, data, len)) {
        sendJson(req, 400, false, "Bad JSON");
        return;
      }

      st.noSsidAutoResetEnabled = d["enabled"] | true;

      uint64_t after = (uint64_t)(d["after_ms"] | WIFI_CONNECT_TIMEOUT_MS);

      if (after < 5000ULL) after = 5000ULL; // min 5s
      if (after > 24ULL*60ULL*60ULL*1000ULL) after = 24ULL*60ULL*60ULL*1000ULL; // max 24h
      st.noSsidAutoResetAfterMs = (uint32_t)after;

      if (st.apMode && st.apStartedMs == 0) {
        st.apStartedMs = millis();
      }

      JsonDocument extra;
      extra["enabled"] = st.noSsidAutoResetEnabled;
      extra["after_ms"] = (uint64_t)st.noSsidAutoResetAfterMs;

      sendJson(req, 200, true, "OK", &extra);
    }
  );

  // ----------------------------
  // UART (RETURN JSON)
  // ----------------------------
  web.on("/api/uart/save", HTTP_POST, [&st](AsyncWebServerRequest* req){}, nullptr,
    [&st](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      JsonDocument d;
      if (deserializeJson(d, data, len)) {
        sendJson(req, 400, false, "Bad JSON");
        return;
      }

      bool a = d["auto"] | true;
      uint32_t b = d["baud"] | CFG_UART_DEFAULT_BAUD;

      st.baudAuto = a;
      Storage::saveUart(a, b);

      if (!st.baudAuto) {
        UartBridge::applyBaud(st, b);
      }

      JsonDocument extra;
      extra["auto"] = st.baudAuto;
      extra["baud"] = b;

      sendJson(req, 200, true, "Saved UART settings.", &extra);
    }
  );

  web.on("/api/uart/autobaud", HTTP_POST, [&st](AsyncWebServerRequest* req){
    uint32_t b = UartBridge::autodetectBaud(st);
    UartBridge::applyBaud(st, b);
    st.baudAuto = false;
    Storage::saveUart(false, b);

    JsonDocument extra;
    extra["auto"] = false;
    extra["baud"] = b;

    sendJson(req, 200, true, String("Autodetect selected ") + b, &extra);
  });

  // ----------------------------
  // Target controls (RETURN JSON)
  // ----------------------------
  web.on("/api/target/reset", HTTP_POST, [](AsyncWebServerRequest* req){
    UartBridge::targetResetPulse(200);
    sendJson(req, 200, true, "Target reset pulsed.");
  });

  web.on("/api/target/fel", HTTP_POST, [](AsyncWebServerRequest* req){
    UartBridge::targetEnterFEL();
    sendJson(req, 200, true, "Enter FEL sequence sent.");
  });

  // Captive portal probes -> redirect
  web.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });

  web.begin();
  D_WEBLN("server started");
}

void WebUi::loop() {
  ws.cleanupClients();
}
