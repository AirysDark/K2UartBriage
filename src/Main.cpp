// ============================================================
// K2 UART Rescue Bridge - main.cpp (FULL COPY/PASTE)
// ESP32-S3 (Arduino + ESPAsyncWebServer)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> 
#include <SD.h>
#include <ArduinoJson.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "Web_pages.h"

// NEW: LittleFS + Crypto (CK2)
#include <LittleFS.h>
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

// Blueprint
#include "DeviceBlueprintLib.h"
#include "BlueprintRuntime.h"

// Config / debug
#include "AppConfig.h"
#include "Debug.h"

// Register this compilation unit as a debug module (for per-module level control)
DBG_REGISTER_MODULE(__FILE__);

// Pins
#include "Pins.h"
#include "Pins_sd.h"

// Core helpers
#include "Util.h"
#include "Env_parse.h"

// Backup/Restore systems
#include "Backup_manager.h"
#include "Restore_manager.h"
#include "Backup_profiles.h"
#include "SafeGuard.h"
#include "SdCache.h"
#include "OTA.h"

// NEW: Restore Plan manifest loader (Linux-mode restore plan JSON)
#include "RestorePlan.h"

// IMPORTANT: use the updated command files you gave
#include "Command.h"

// Hidden WebSocket / UI bridge
#include "K2BUI.h"


#include <map>


// ============================================================
// Globals
// ============================================================
Preferences prefs;
DNSServer dns;

AsyncWebServer web(CFG_WEB_PORT);

// WebSocket channels:
//  - /ws   : secured websocket intended for external tools (requires CK2 auth)
//  - /wsui : local WebUI console websocket (no auth)
AsyncWebSocket ws("/ws");
AsyncWebSocket wsui("/wsui");

AsyncServer tcpServer(CFG_TCP_PORT);
AsyncClient* tcpClient = nullptr;

struct WsSession {
  bool authed = false;
  uint32_t lastCmdMs = 0;
};

static std::map<uint32_t, WsSession> g_wsSessions;

static WsSession& wsSess(AsyncWebSocketClient* c) {
  return g_wsSessions[c ? c->id() : 0];
}

static void wsSessDrop(AsyncWebSocketClient* c) {
  if (!c) return;
  g_wsSessions.erase(c->id());
}

HardwareSerial TargetSerial(2);
static Preferences wifiPrefs;
static bool     apMode = false;
static uint32_t apStartedMs = 0;

static uint32_t currentBaud = CFG_UART_DEFAULT_BAUD;
static bool     baudAuto = true;

// Runtime AP auto-reset config (stored in Preferences)
static bool     noSsidAutoResetEnabled = true;
static uint32_t noSsidAutoResetAfterMs = 5UL * 60UL * 1000UL; // default 5 min

// Backup/Restore
static BackupManager backupMgr;
static RestoreManager restoreMgr;
static std::vector<uint8_t> restoreFileBuf;

// NEW: manifest-based restore plan
static RestorePlan gRestore;

// ===== autobaud scheduling state =====
static volatile bool autoBaudRequested = false;
static volatile bool autoBaudRunning   = false;
static uint32_t      autoBaudResult    = 0;
static String        autoBaudStatus    = "idle";

// ============================================================
// U-Boot state tracking (Task A - UMS)
// ============================================================
static bool     ubootPromptSeen = false;
static uint32_t ubootPromptLastMs = 0;

static bool     umsActive = false;
static uint32_t umsStartedMs = 0;

// ============================================================
// Safe env capture + restore safety state
// ============================================================
static bool     envCapActive = false;
static bool     envCapArmed  = false;
static uint32_t envCapStartMs = 0;
static String   envCapBuf;
static String   lastEnvText;
static String   lastEnvBoardId;
static String   lastEnvLayoutJson;

// Restore safety/session (legacy flag still used by web endpoints)
static bool     restoreArmed = false;
static String   restoreAckToken;
static bool     restoreBoardOverride = false;

// ============================================================
// Command system context
// ============================================================
static Command::Context gCmdCtx;

// ============================================================
// Per-source passthrough line buffers (for NON-! lines)
// Command handles !commands; these forward normal lines to target
// ============================================================
static String ptUsb;
static String ptWs;
static String ptTcp;

static inline String& ptBuf(Command::Source src) {
  switch (src) {
    case Command::Source::USB: return ptUsb;
    case Command::Source::WS:  return ptWs;
    case Command::Source::TCP: return ptTcp;
    default: return ptUsb;
  }
}

// ============================================================
// Helpers
// ============================================================
static inline String makeAckToken() {
  const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  char t[7]; t[6] = 0;
  for (int i=0;i<6;i++) t[i] = alphabet[esp_random() % 32];
  return String(t);
}

static inline uint32_t estimateSecondsFromBytes(uint64_t bytes, uint32_t baud) {
  double bytesPerSec = (double)baud / 10.0 * 0.60; // 10 bits/byte + 60% efficiency
  if (bytesPerSec < 1.0) bytesPerSec = 1.0;
  return (uint32_t)ceil((double)bytes / bytesPerSec);
}

static inline bool ubootPromptFresh(uint32_t maxAgeMs = 2500) {
  return ubootPromptSeen && ((millis() - ubootPromptLastMs) <= maxAgeMs);
}

// IMPORTANT: ensure timer stamp is NEVER 0
static inline void armApTimerNow() {
  uint32_t m = millis();
  apStartedMs = (m == 0) ? 1 : m;
}

static inline void clearApTimer() { apStartedMs = 0; }
static inline bool apTimerArmed() { return apStartedMs != 0; }
static inline uint32_t apElapsedMs() { return apTimerArmed() ? (uint32_t)(millis() - apStartedMs) : 0; }

// ============================================================
// CR/LF NORMALIZATION FIX (PuTTY sends CR-only by default)
// Convert \r -> \n before feeding Command system
// ============================================================
static void feedNormalized(Command::Source src, const uint8_t* data, size_t len) {
  static uint8_t tmp[512];
  if (!data || !len) return;

  size_t n = (len > sizeof(tmp)) ? sizeof(tmp) : len;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = data[i];
    tmp[i] = (c == '\r') ? '\n' : c;
  }
  Command::feed(src, tmp, n);
}

// ============================================================
// Wi-Fi creds helpers
// ============================================================
static void saveWifiCreds(const String& ssid, const String& pass) {
  wifiPrefs.begin("bridge", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.end();
}

static bool loadWifiCreds(String& ssid, String& pass) {
  wifiPrefs.begin("bridge", true);
  ssid = wifiPrefs.getString("ssid", "");
  pass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
  return ssid.length() > 0;
}

static bool verifyWifiCreds(const String& ssid, const String& pass) {
  String s2, p2;
  bool ok = loadWifiCreds(s2, p2);
  return ok && (s2 == ssid) && (p2 == pass);
}

static bool haveSavedSsid() {
  String ssid, pass;
  return loadWifiCreds(ssid, pass);
}

static void clearWifiCreds() {
  wifiPrefs.begin("bridge", false);
  wifiPrefs.remove("ssid");
  wifiPrefs.remove("pass");
  wifiPrefs.end();
}

// ============================================================
// Runtime AP auto-reset config (Preferences)
// ============================================================
static void loadApResetConfig() {
  prefs.begin("bridge", true);
  noSsidAutoResetEnabled = prefs.getBool("noSsidEn", true);
  noSsidAutoResetAfterMs = prefs.getUInt("noSsidMs", 5UL * 60UL * 1000UL);
  prefs.end();

  // clamp: 15s min, 24h max
  if (noSsidAutoResetAfterMs < 15000UL) noSsidAutoResetAfterMs = 15000UL;
  if (noSsidAutoResetAfterMs > 24UL * 60UL * 60UL * 1000UL) noSsidAutoResetAfterMs = 24UL * 60UL * 60UL * 1000UL;
}

static void saveApResetConfig(bool enabled, uint32_t afterMs) {
  if (afterMs < 15000UL) afterMs = 15000UL;
  if (afterMs > 24UL * 60UL * 60UL * 1000UL) afterMs = 24UL * 60UL * 60UL * 1000UL;

  prefs.begin("bridge", false);
  prefs.putBool("noSsidEn", enabled);
  prefs.putUInt("noSsidMs", afterMs);
  prefs.end();

  noSsidAutoResetEnabled = enabled;
  noSsidAutoResetAfterMs = afterMs;
}

// ============================================================
// UART config
// ============================================================
static void saveUartConfig(bool autoBaud, uint32_t baud) {
  prefs.begin("bridge", false);
  prefs.putBool("baudAuto", autoBaud);
  prefs.putUInt("baud", baud);
  prefs.end();
}

static void loadUartConfig() {
  prefs.begin("bridge", true);
  baudAuto = prefs.getBool("baudAuto", true);
  currentBaud = prefs.getUInt("baud", CFG_UART_DEFAULT_BAUD);
  prefs.end();
}

// Backwards-compatible WebUI helpers
// - applyTargetBaud(): set Target UART baud immediately
// - saveUartSettings(): persist settings + apply baud if auto-baud is disabled
static void applyTargetBaud(uint32_t baud) {
  currentBaud = baud;
  TargetSerial.updateBaudRate(baud);
  DBG_PRINTF("[UART] Target baud set to %lu\\n", (unsigned long)baud);
}

static void saveUartSettings(bool autoBaud, uint32_t baud) {
  saveUartConfig(autoBaud, baud);
  baudAuto = autoBaud;
  if (!baudAuto) {
    applyTargetBaud(baud);
  }
}

static bool isPrintable(uint8_t b) {
  return (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20 && b <= 0x7E));
}

// Baud autodetect heuristic
static uint32_t autodetectBaud(uint32_t sampleMs = 700) {
  const uint32_t candidates[] = {115200,57600,38400,19200,9600,230400,460800,921600};
  uint32_t bestBaud = currentBaud;
  float bestScore = -1.0f;

  for (uint32_t b : candidates) {
    TargetSerial.updateBaudRate(b);
    delay(50);

    uint32_t start = millis();
    size_t total=0, printable=0, zeros=0;

    while (millis() - start < sampleMs) {
      while (TargetSerial.available()) {
        uint8_t c = (uint8_t)TargetSerial.read();
        total++;
        if (c == 0x00) zeros++;
        if (isPrintable(c)) printable++;
        if (total >= 512) break;
      }
      if (total >= 512) break;
      delay(2);
    }

    float pr = total ? (float)printable / (float)total : 0.0f;
    float z  = total ? (float)zeros / (float)total : 0.0f;
    float bytesFactor = (float)std::min<size_t>(total, 256) / 256.0f;
    float score = (total < 16) ? -1.0f : (pr * bytesFactor - (z * 0.25f));

    DBG_PRINTF("[AUTOBAUD] %lu -> total=%u pr=%.2f z=%.2f score=%.3f\n",
               (unsigned long)b, (unsigned)total, pr, z, score);

    if (score > bestScore) { bestScore = score; bestBaud = b; }
  }

  TargetSerial.updateBaudRate(bestBaud);
  DBG_PRINTF("[AUTOBAUD] Selected %lu (score=%.3f)\n", (unsigned long)bestBaud, bestScore);
  return bestBaud;
}

// ============================================================
// Target control
// ============================================================
static void targetResetPulse(uint32_t ms = 200) {
  digitalWrite(PIN_TARGET_RESET, LOW);
  delay(ms);
  digitalWrite(PIN_TARGET_RESET, HIGH);
}

static void targetEnterFEL() {
  digitalWrite(PIN_TARGET_FEL, LOW);
  delay(50);
  targetResetPulse(200);
  delay(600);
  digitalWrite(PIN_TARGET_FEL, HIGH);
}

// ============================================================
// WiFi modes
// ============================================================
static void startAP() {
  apMode = true;
  armApTimerNow(); // always arm timer (non-zero stamp)

  dns.stop();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  const bool ok = WiFi.softAP(CFG_WIFI_AP_SSID, CFG_WIFI_AP_PASS);
  dns.start(DNS_PORT, "*", AP_IP);

  DBG_PRINTF("[WIFI] AP started: ok=%d ssid=%s ip=%s\n",
             ok ? 1 : 0, CFG_WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

static bool startSTAWithTimeout() {
  apMode = false;
  clearApTimer();

  dns.stop();

  String ssid, pass;
  if (!loadWifiCreds(ssid, pass)) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  DBG_PRINTF("[WIFI] STA connect start: ssid=%s\n", ssid.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    led_set((((millis()-start)/500) % 2) == 0);
  }
  led_set(false);

  if (WiFi.status() == WL_CONNECTED) {
    DBG_PRINTF("[WIFI] STA connected: ip=%s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  DBG_PRINTF("[WIFI] STA timeout -> fallback AP\n");
  WiFi.disconnect(true, true);
  return false;
}

// ============================================================
// Reply routing for Command system
// ============================================================
static void cmdReply(Command::Source src, const char* msg) {
  if (!msg) return;
  switch (src) {
    case Command::Source::USB:
      Serial.print(msg);
      break;
    case Command::Source::WS:
      ws.textAll(msg);
      break;
    case Command::Source::TCP:
      if (tcpClient && tcpClient->connected()) tcpClient->write(msg, strlen(msg));
      break;
  }
}

static void cmdReplyLn(Command::Source src, const char* msg) {
  if (!msg) msg = "";
  String s(msg);
  s += "\n";
  cmdReply(src, s.c_str());
}

// ============================================================
// K2BUI command output capture (for WebUI console)
// - Command::Context uses function pointers, so we can't use capturing lambdas.
// - We temporarily swap gCmdCtx.reply/replyLn to these capture functions.
// ============================================================
static bool   gK2buiCaptureOn = false;
static String gK2buiCapture;

static void k2buiCaptureReply(Command::Source /*src*/, const char* msg) {
  if (!gK2buiCaptureOn || !msg) return;
  gK2buiCapture += msg;
}

static void k2buiCaptureReplyLn(Command::Source /*src*/, const char* msg) {
  if (!gK2buiCaptureOn) return;
  if (!msg) msg = "";
  gK2buiCapture += msg;
  gK2buiCapture += "\n";
}

// ============================================================
// IMPORTANT FIX: ingest from ANY client (USB/WS/TCP)
// - feedNormalized() fixes CR-only clients (PuTTY)
// - forwards NON-! lines to target (printer cmds still work)
// ============================================================
static void ingestFromClient(Command::Source src, const uint8_t* data, size_t len) {
  if (!data || !len) return;

  // 1) Parse local !commands (normalized)
  feedNormalized(src, data, len);

  // 2) Forward non-! lines to target (line-based)
  String& b = ptBuf(src);

  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];

    if (c == '\r') c = '\n';

    if (c == '\n') {
      String line = b;
      b = "";
      line.trim();

      if (line.length() == 0) {
        TargetSerial.print("\n");
        continue;
      }

      if (line.length() && line[0] == '!') {
        continue;
      }

      TargetSerial.print(line);
      TargetSerial.print("\n");
      continue;
    }

    b += c;
    if (b.length() > 2048) b.remove(0, b.length() - 512);
  }
}

// ============================================================
// Wire Command context callbacks
// ============================================================
static void setupCommandContext() {
  memset(&gCmdCtx, 0, sizeof(gCmdCtx));

  gCmdCtx.reply   = cmdReply;
  gCmdCtx.replyLn = cmdReplyLn;

  // passthrough to target UART
  gCmdCtx.targetWrite = [](const uint8_t* data, size_t len) {
    if (!data || !len) return;
    TargetSerial.write(data, len);
  };
  gCmdCtx.targetWriteLine = [](const String& line) {
    TargetSerial.print(line);
    TargetSerial.print("\n");
  };

  // status getters
  gCmdCtx.isApMode = []() -> bool { return apMode; };
  gCmdCtx.haveSavedSsid = []() -> bool { return haveSavedSsid(); };
  gCmdCtx.apElapsedMs = []() -> uint32_t { return apMode ? apElapsedMs() : 0U; };
  gCmdCtx.apTimerAfterMs = []() -> uint32_t { return noSsidAutoResetAfterMs; };
  gCmdCtx.apTimerEnabled = []() -> bool { return noSsidAutoResetEnabled; };
  gCmdCtx.ipNow = []() -> IPAddress { return apMode ? WiFi.softAPIP() : WiFi.localIP(); };

  gCmdCtx.uartGetBaud = []() -> uint32_t { return currentBaud; };
  gCmdCtx.uartGetAuto = []() -> bool { return baudAuto; };

  gCmdCtx.otaInProgress = []() -> bool { return OTA::inProgress(); };
  gCmdCtx.otaWritten    = []() -> uint32_t { return OTA::progressBytes(); };
  gCmdCtx.otaTotal      = []() -> uint32_t { return OTA::totalBytes(); };

  gCmdCtx.sdStatusJson = []() -> String {
    JsonDocument d;
    d["mounted"] = SdCache::mounted();
    d["backup_exists"] = SdCache::exists(SdItem::Backup);
    d["backup_size"] = (uint32_t)SdCache::sizeBytes(SdItem::Backup);
    d["firmware_exists"] = SdCache::exists(SdItem::Firmware);
    d["firmware_size"] = (uint32_t)SdCache::sizeBytes(SdItem::Firmware);
    String out;
    serializeJson(d, out);
    return out;
  };

  gCmdCtx.ubootPromptFresh = []() -> bool { return ubootPromptFresh(2500); };
  gCmdCtx.umsIsActive      = []() -> bool { return umsActive; };
  gCmdCtx.envLastText      = []() -> String { return lastEnvText; };
  gCmdCtx.envLastBoardId   = []() -> String { return lastEnvBoardId; };
  gCmdCtx.envLastLayoutJson= []() -> String { return lastEnvLayoutJson; };

  // minimal backup/restore read-only wiring
  gCmdCtx.backupStatusLine = []() -> String { return backupMgr.running() ? backupMgr.statusLine() : String("idle"); };
  gCmdCtx.backupProgress01 = []() -> float { return backupMgr.running() ? backupMgr.progress() : 0.0f; };
  gCmdCtx.backupGetProfileId = []() -> String { return backupMgr.getProfileId(); };
  gCmdCtx.backupGetCustomRange = [](uint32_t& start, uint32_t& count) {
    backupMgr.getCustomRange(start, count);
  };

  // ===========================
  // Restore Manifest (NEW)
  // ===========================
  gCmdCtx.restoreIsLoaded = []() -> bool {
    return gRestore.isLoaded() || restoreMgr.isLoaded();
  };

  gCmdCtx.restoreIsArmed  = []() -> bool {
    return restoreArmed || gRestore.isArmed();
  };

  gCmdCtx.restorePlan = []() -> String {
    if (gRestore.isLoaded()) return gRestore.planText();
    if (restoreMgr.isLoaded()) return String("[restore] legacy restoreMgr loaded (no manifest planText available)\n");
    return String("(no restore plan loaded)\n");
  };

  gCmdCtx.restoreVerify = []() -> String {
    if (gRestore.isLoaded()) return gRestore.verifyText();
    return String("restore verify: FAIL (manifest not loaded)\n");
  };

  gCmdCtx.restoreArm = [](const String& token, bool overrideBoardId) -> String {
    restoreArmed = true;
    restoreBoardOverride = overrideBoardId;
    restoreAckToken = token.length() ? token : makeAckToken();

    if (gRestore.isLoaded()) {
      return gRestore.arm(restoreAckToken, overrideBoardId);
    }
    return String("restore arm: OK (legacy flag set, but no manifest loaded)");
  };

  gCmdCtx.restoreDisarm = []() {
    restoreArmed = false;
    restoreAckToken = "";
    restoreBoardOverride = false;
    gRestore.disarm();
  };

  gCmdCtx.restoreApply = []() -> String {
    if (gRestore.isLoaded()) return gRestore.applyText();
    return String("restore apply: FAIL (manifest not loaded)\n");
  };

  // actions
  gCmdCtx.rebootNow = []() { ESP.restart(); };

  gCmdCtx.wifiSave = [](const String& ssid, const String& pass) {
    saveWifiCreds(ssid, pass);
  };
  gCmdCtx.wifiReset = []() {
    clearWifiCreds();
  };

  gCmdCtx.forceApNow = []() {
    WiFi.disconnect(true, true);
    startAP();
  };
  gCmdCtx.forceStaNow = []() -> bool {
    return startSTAWithTimeout();
  };

  // "ap timer" commands map to your "no-ssid auto reset" settings
  gCmdCtx.apTimerReset = []() { armApTimerNow(); };
  gCmdCtx.apTimerSetAfterMs = [](uint32_t ms) { saveApResetConfig(noSsidAutoResetEnabled, ms); };
  gCmdCtx.apTimerSetEnabled = [](bool en) { saveApResetConfig(en, noSsidAutoResetAfterMs); };

  gCmdCtx.uartSetBaud = [](uint32_t b) {
    baudAuto = false;
    saveUartConfig(false, b);
    applyTargetBaud(b);
  };
  gCmdCtx.uartSetAuto = [](bool en) {
    baudAuto = en;
    saveUartConfig(en, currentBaud);
  };
  gCmdCtx.uartRunAutodetectNow = []() {
    if (autoBaudRunning) return;
    autoBaudRequested = true;
  };

  gCmdCtx.targetResetPulseMs = [](uint32_t ms) { targetResetPulse(ms); };
  gCmdCtx.targetEnterFel     = []() { targetEnterFEL(); };

  gCmdCtx.umsStart = []() {
    if (!ubootPromptFresh(2500)) return;
    TargetSerial.print("ums 0 mmc 0\n");
    umsActive = true;
    umsStartedMs = millis();
  };
  gCmdCtx.umsClear = []() {
    uint8_t c = 0x03; // Ctrl+C
    TargetSerial.write(&c, 1);
    umsActive = false;
    umsStartedMs = 0;
  };

  gCmdCtx.envCaptureStart = []() {
    if (!ubootPromptFresh(2500)) return;
    envCapBuf = "";
    envCapActive = true;
    envCapArmed  = true;
    envCapStartMs = millis();
    TargetSerial.print("printenv\n");
  };

  // ===================== SafeGuard hooks =====================
  gCmdCtx.sgIsUnsafe = []() -> bool { return SafeGuard::isUnsafe(); };
  gCmdCtx.sgSetUnsafe = [](bool on) { SafeGuard::setUnsafe(on); };
  gCmdCtx.sgUnsafeRemainingMs = []() -> uint32_t { return SafeGuard::unsafeRemainingMs(); };

  Command::begin(&gCmdCtx);
}

// ============================================================
// TCP UART server
// ============================================================
static void startTcpServer() {
  tcpServer.onClient([](void* arg, AsyncClient* c) {
    (void)arg;
    if (tcpClient) {
      c->write("BUSY: another client is connected.\n");
      c->close(true);
      return;
    }
    tcpClient = c;
    DBG_PRINTF("[TCP] client connected\n");

    c->onData([](void* arg2, AsyncClient* client, void* data, size_t len) {
      (void)arg2;
      if (!client || client != tcpClient) return;
      ingestFromClient(Command::Source::TCP, (uint8_t*)data, len);
    }, nullptr);

    c->onDisconnect([](void* arg3, AsyncClient* client) {
      (void)arg3;
      DBG_PRINTF("[TCP] client disconnected\n");
      if (tcpClient == client) tcpClient = nullptr;
    }, nullptr);

  }, nullptr);

  tcpServer.begin();
  DBG_PRINTF("[TCP] listening on %u\n", CFG_TCP_PORT);
}

// ============================================================
// Web routes
// ============================================================
static bool isCaptiveRequest(AsyncWebServerRequest* request) {
  if (!apMode) return false;
  if (!request->hasHeader("Host")) return false;
  String host = request->header("Host");
  if (host.indexOf(AP_IP.toString()) >= 0) return false;
  return true;
}

// ============================================================
// Hidden WS token (glue-only)
// ============================================================
static String getHiddenWsToken() {
  // Option A: hardcode (quick)
  // return "CHANGE_ME_TOKEN";

  // Option B: store in prefs (better)
  prefs.begin("bridge", true);
  String t = prefs.getString("wsToken", "");
  prefs.end();
  if (t.length() == 0) t = "CHANGE_ME_TOKEN";
  return t;
}

// ============================================================
// CK2 - Generation + AES-256-GCM + LittleFS storage
// ============================================================

static const char* CK2_FS_DIR      = "/ck2";
static const char* CK2_LAST_PATH   = "/ck2/last.ck2";
static const char* CK2_PREF_NS     = "bridge";
static const char* CK2_PREF_KEY    = "ck2_master";

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

static String ck2Base64Encode(const uint8_t* data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  std::vector<uint8_t> out;
  out.resize(olen + 1);
  if (mbedtls_base64_encode(out.data(), out.size(), &olen, data, len) != 0) return "";
  out[olen] = 0;
  return String((const char*)out.data());
}

static bool ck2Base64Decode(const char* b64, std::vector<uint8_t>& out) {
  if (!b64) return false;
  size_t ilen = strlen(b64);
  size_t olen = 0;
  mbedtls_base64_decode(nullptr, 0, &olen, (const unsigned char*)b64, ilen);
  out.resize(olen);
  return (mbedtls_base64_decode(out.data(), out.size(), &olen, (const unsigned char*)b64, ilen) == 0);
}

// ============================================================
// WS auth state (per-client)  (NO getUserData/setUserData)
// Some ESPAsyncWebServer forks removed userData.
// We'll keep a small auth table keyed by client->id().
// ============================================================
static const int MAX_WS_AUTH = 12; // tune if needed
static uint32_t gWsAuthIds[MAX_WS_AUTH] = {0};
static bool     gWsAuthOk[MAX_WS_AUTH]  = {false};

static int wsAuthFind(uint32_t id) {
  for (int i = 0; i < MAX_WS_AUTH; ++i) if (gWsAuthIds[i] == id) return i;
  return -1;
}
static int wsAuthFree() {
  for (int i = 0; i < MAX_WS_AUTH; ++i) if (gWsAuthIds[i] == 0) return i;
  return -1;
}
static inline bool wsIsAuthedId(uint32_t id) {
  int s = wsAuthFind(id);
  return (s >= 0) ? gWsAuthOk[s] : false;
}
static inline bool wsIsAuthed(AsyncWebSocketClient* c) {
  return c ? wsIsAuthedId(c->id()) : false;
}
static inline void wsSetAuthed(AsyncWebSocketClient* c, bool on) {
  if (!c) return;
  uint32_t id = c->id();
  int s = wsAuthFind(id);
  if (s < 0) {
    s = wsAuthFree();
    if (s < 0) return; // fail closed if full
    gWsAuthIds[s] = id;
  }
  gWsAuthOk[s] = on;
  if (!on) {
    // reclaim slot immediately to avoid filling table over time
    gWsAuthIds[s] = 0;
    gWsAuthOk[s]  = false;
  }
}

// ============================================================
// Web setup  (FULL REPLACEMENT - SINGLE FUNCTION)
// - Serves WebUI from LittleFS (root -> /www)
// - Public WS (/ws) auth via CK2 base64
// - Hidden WS via K2BUI (/_sys/ws)
// - CK2 API downloads from LittleFS file (reliable)
// - Captive portal safe onNotFound
// ============================================================
static void setupWeb() {

  // Ensure LittleFS is mounted (safe to call even if already mounted)
  if (!LittleFS.begin(true)) {
    DBG_PRINTF("[LFS] mount failed (web)\n");
  } else {
    DBG_PRINTF("[LFS] mount ok (web)\n");
    if (!LittleFS.exists("/www")) {
      // You said you created /www. If it doesn't exist, UI uploadfs didn't happen.
      DBG_PRINTF("[LFS] WARNING: /www missing (did you uploadfs?)\n");
    }
    if (!LittleFS.exists(CK2_FS_DIR)) LittleFS.mkdir(CK2_FS_DIR);
  }

  // --------------------------
  // Public console websocket (/ws)
  // --------------------------
  ws.onEvent([](AsyncWebSocket * server, AsyncWebSocketClient * client,
                AwsEventType type, void * arg, uint8_t *data, size_t len) {
    (void)server;

    if (type == WS_EVT_CONNECT) {
      wsSetAuthed(client, false);
      client->text("[WS] connected. Send: !auth <base64_ck2>\n");
      return;
    }

    if (type == WS_EVT_DISCONNECT) {
      wsSetAuthed(client, false);
      return;
    }

    if (type != WS_EVT_DATA) return;

    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info || !info->final || info->opcode != WS_TEXT) {
      client->text("[WS] text-only (auth required)\n");
      return;
    }

    String msg;
    msg.reserve(len + 1);
    for (size_t i = 0; i < len; i++) msg += (char)data[i];
    msg.trim();

    if (!wsIsAuthed(client)) {
      if (!msg.startsWith("!auth ")) {
        client->text("[WS] NOT AUTHED. Use: !auth <base64_ck2>\n");
        return;
      }

      String b64 = msg.substring(6);
      b64.trim();

      std::vector<uint8_t> ck2;
      if (!ck2Base64Decode(b64.c_str(), ck2) || ck2.size() < (sizeof(CK2Header) + 16)) {
        client->text("[WS] auth fail: base64\n");
        return;
      }

      String json, err;
      if (!ck2VerifyAndExtract(ck2.data(), ck2.size(), json, err)) {
        client->text(String("[WS] auth fail: ") + err + "\n");
        return;
      }

      wsSetAuthed(client, true);
      client->text("[WS] auth OK\n");
      return;
    }

    // Auth OK -> normal ingest pipeline
    ingestFromClient(Command::Source::WS, data, len);
  });

  web.addHandler(&ws);

  // --------------------------
  // Hidden WS via K2BUI (/_sys/ws)
  // --------------------------
  {
    K2BUI::Callbacks cb;

    cb.uart_write = [](const uint8_t* d, size_t n) {
      if (!d || !n) return;
      TargetSerial.write(d, n);
    };

    // WebUI console: execute local !commands and return their output back over K2BUI.
    // CK2 auth belongs to the public /ws endpoint (external tools), not the WebUI.
    cb.icommand_exec = [](const String& line) -> String {
      String cmd = line;
      cmd.trim();
      if (cmd.length() == 0) return String("");

      // This console is for LOCAL commands. Those are prefixed with '!'.
      // If you type help/ihelp, we translate to !help for convenience.
      if (!cmd.startsWith("!")) {
        if (cmd.equalsIgnoreCase("help") || cmd.equalsIgnoreCase("ihelp")) {
          cmd = "!help";
        } else {
          return String("This console executes local commands with '!' prefix. Try: !help");
        }
      }

      // Swap the command reply routing to a capture buffer for the duration of this command.
      auto oldReply   = gCmdCtx.reply;
      auto oldReplyLn = gCmdCtx.replyLn;
      gK2buiCapture = "";
      gK2buiCapture.reserve(512);
      gK2buiCaptureOn = true;
      gCmdCtx.reply = k2buiCaptureReply;
      gCmdCtx.replyLn = k2buiCaptureReplyLn;

      (void)Command::feedText(Command::Source::USB, cmd.c_str());
      (void)Command::feedText(Command::Source::USB, "\n");

      // Restore routing
      gCmdCtx.reply = oldReply;
      gCmdCtx.replyLn = oldReplyLn;
      gK2buiCaptureOn = false;

      String out = gK2buiCapture;
      out.trim();
      if (out.length() == 0) return String("(ok)");
      return out;
    };

    cb.ibp_exec = [](const String& jsonPayload) -> String {
      (void)jsonPayload;
      return String("{\"t\":\"ibp\",\"ok\":false,\"msg\":\"ibp not wired yet\"}");
    };

    cb.auth_check = [](const String& token) -> bool {
      return token == getHiddenWsToken();
    };

    K2BUI::begin(web, cb);
  }

  // --------------------------
  // OTA endpoints
  // --------------------------
  OTA::attach(web);

  // If you moved ota.html into /www, serve that.
  web.on("/ota", HTTP_GET, [](AsyncWebServerRequest* req){
    if (isCaptiveRequest(req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    if (LittleFS.begin(true) && LittleFS.exists("/www/ota.html")) {
      req->send(LittleFS, "/www/ota.html", "text/html");
      return;
    }
    // fallback to PROGMEM if you still have it
    req->send_P(200, "text/html", OTA_HTML);
  });

  // --------------------------
  // UI from LittleFS (ROOT)
  // Put files in: data/www/*
  // Upload with: pio run -t uploadfs
  // --------------------------
  // Serve /www at root:
  //   /index.html, /app.js, /app.css, /console.html, /ota.html etc
  web.serveStatic("/", LittleFS, "/www")
     .setDefaultFile("index.html")
     .setCacheControl("no-store");   // dev-safe

  // Optional explicit types (some browsers are picky)
  web.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req){
    if (LittleFS.begin(true) && LittleFS.exists("/www/app.js")) {
      req->send(LittleFS, "/www/app.js", "application/javascript");
      return;
    }
    req->send(404, "text/plain", "app.js missing (uploadfs?)");
  });

  web.on("/app.css", HTTP_GET, [](AsyncWebServerRequest* req){
    if (LittleFS.begin(true) && LittleFS.exists("/www/app.css")) {
      req->send(LittleFS, "/www/app.css", "text/css");
      return;
    }
    req->send(404, "text/plain", "app.css missing (uploadfs?)");
  });

  // Root handler (captive safe)
  web.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    if (isCaptiveRequest(req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    if (LittleFS.begin(true) && LittleFS.exists("/www/index.html")) {
      req->send(LittleFS, "/www/index.html", "text/html");
      return;
    }
    // fallback if LittleFS UI missing
    req->send_P(200, "text/html", INDEX_HTML);
  });

  // Console route: if you moved console.html into /www, serve it.
  web.on("/console", HTTP_GET, [](AsyncWebServerRequest* req){
    if (isCaptiveRequest(req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    if (LittleFS.begin(true) && LittleFS.exists("/www/console.html")) {
      req->send(LittleFS, "/www/console.html", "text/html");
      return;
    }
    req->send_P(200, "text/html", CONSOLE_HTML);
  });

// ----------------------------
  // Wi-Fi (RETURN JSON, avoids blank popup on fast reboot)
  // ----------------------------
  web.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      JsonDocument d;
      if (deserializeJson(d, data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"msg\":\"Bad JSON\"}");
        return;
      }

      String ssid = d["ssid"] | "";
      String pass = d["pass"] | "";
      if (ssid.length() < 1) {
        req->send(400, "application/json", "{\"ok\":false,\"msg\":\"SSID required\"}");
        return;
      }

      saveWifiCreds(ssid, pass);

      // respond FIRST, then reboot (delay to let TCP flush)
      req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved Wi-Fi. Rebooting...\"}");
      delay(450);
      ESP.restart();
    }
  );

  web.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* req){
    clearWifiCreds();
    req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Cleared Wi-Fi creds. Rebooting...\"}");
    delay(450);
    ESP.restart();
  });



  // ============================================================
  // CK2 API (RELIABLE FILE DOWNLOADS)
  // ============================================================

  // POST /api/ck2/generate?ttl=604800
  web.on("/api/ck2/generate", HTTP_POST, [](AsyncWebServerRequest* req){
    uint32_t ttl = 7UL * 24UL * 3600UL;
    if (req->hasParam("ttl")) {
      ttl = (uint32_t)req->getParam("ttl")->value().toInt();
      if (ttl < 60) ttl = 60;
      if (ttl > 30UL * 24UL * 3600UL) ttl = 30UL * 24UL * 3600UL;
    }

    if (!LittleFS.begin(true)) {
      req->send(500, "text/plain", "LittleFS not mounted");
      return;
    }
    if (!LittleFS.exists(CK2_FS_DIR)) LittleFS.mkdir(CK2_FS_DIR);

    std::vector<uint8_t> ck2;
    if (!ck2GenerateFile(ck2, ttl)) {
      req->send(500, "text/plain", "CK2 generate failed");
      return;
    }

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

    AsyncWebServerResponse* res =
      req->beginResponse(LittleFS, CK2_LAST_PATH, "application/octet-stream", true);
    res->addHeader("Content-Disposition", "attachment; filename=CK2.key");
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  // GET /api/ck2/download
  web.on("/api/ck2/download", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!LittleFS.begin(true)) {
      req->send(500, "text/plain", "LittleFS not mounted");
      return;
    }
    if (!LittleFS.exists(CK2_LAST_PATH)) {
      req->send(404, "text/plain", "No CK2 generated");
      return;
    }

    AsyncWebServerResponse* res =
      req->beginResponse(LittleFS, CK2_LAST_PATH, "application/octet-stream", true);
    res->addHeader("Content-Disposition", "attachment; filename=CK2.key");
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  // GET /api/ck2/verify_last
  web.on("/api/ck2/verify_last", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!LittleFS.begin(true)) { req->send(500, "text/plain", "LittleFS not mounted"); return; }
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
    String out; serializeJson(d, out);

    req->send(ok ? 200 : 401, "application/json", out);
  });

  // ----------------------------
  // UART (RETURN JSON)
  // ----------------------------
  web.on("/api/uart/save", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      JsonDocument d;
      if (deserializeJson(d, data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"msg\":\"Bad JSON\"}");
        return;
      }
      bool autoBaud = d["auto"] | true;
      uint32_t baud = d["baud"] | CFG_UART_DEFAULT_BAUD;
      saveUartSettings(autoBaud, baud);
      req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved UART settings.\"}");
    }
  );

  // ------------------------------------------------------------
  // Captive helpers (android/ios)
  // ------------------------------------------------------------
  web.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  web.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });

  // IMPORTANT: notFound handler prevents asset requests being hijacked badly
  web.onNotFound([](AsyncWebServerRequest* req){
    if (isCaptiveRequest(req)) {
      req->redirect(String("http://") + AP_IP.toString() + "/");
      return;
    }
    // If file exists at root, serve it (root static is /www)
    if (LittleFS.begin(true)) {
      String p = req->url();
      if (p == "/") p = "/index.html";
      String tryPath = String("/www") + p;
      if (LittleFS.exists(tryPath)) {
        req->send(LittleFS, tryPath);
        return;
      }
    }
    req->send(404, "text/plain", "Not found");
  });

  // Start server
  web.begin();
  DBG_PRINTF("[WEB] server started\n");
 }

// ============================================================
// Bridge pump (UPDATED: adds hidden WS UART RX tap)
// ============================================================
static void pumpTargetToOutputs() {
  uint8_t buf[256];
  size_t n = 0;

  while (TargetSerial.available() && n < sizeof(buf)) {
    buf[n++] = (uint8_t)TargetSerial.read();
  }
  if (!n) return;

  BlueprintRuntime::feedBytes(buf, n);
  K2BUI::onUartRx(buf, n);

  static char last1 = 0;
  static String line;

  for (size_t i = 0; i < n; i++) {
    const char c = (char)buf[i];

    if (envCapActive) {
      envCapBuf += c;
      if (envCapBuf.length() > 160 * 1024) envCapBuf.remove(0, envCapBuf.length() - 160 * 1024);
    }

    if (last1 == '=' && c == '>') {
      ubootPromptSeen = true;
      ubootPromptLastMs = millis();

      if (envCapActive && envCapArmed && (millis() - envCapStartMs) > 200 && envCapBuf.length() > 64) {
        lastEnvText = envCapBuf;
        lastEnvBoardId = EnvParse::inferBoardId(lastEnvText);
        lastEnvLayoutJson = EnvParse::layoutHintJson(lastEnvText);
        envCapActive = false;
        envCapArmed  = false;
      }
    }

    if (c == '\n' || c == '\r') {
      if (line.length()) {
        BlueprintRuntime::feedLine(line);
      }

      if (line.indexOf("Linux version") >= 0 ||
          line.indexOf("Starting kernel") >= 0 ||
          line.indexOf("login:") >= 0 ||
          line.indexOf("BusyBox") >= 0 ||
          line.indexOf("[    0.000000]") >= 0) {
        ubootPromptSeen = false;
      }

      line = "";
    } else if ((uint8_t)c >= 0x20 && (uint8_t)c <= 0x7E) {
      line += c;
      if (line.length() > 96) line.remove(0, line.length() - 96);
    }

    last1 = c;
  }

  backupMgr.onTargetBytes(buf, n);
  restoreMgr.onTargetBytes(buf, n);

  Serial.write(buf, n);

  if (tcpClient && tcpClient->connected()) tcpClient->write((const char*)buf, n);

  String s; s.reserve(n);
  for (size_t i = 0; i < n; i++) s += (char)buf[i];
  ws.textAll(s);
}

// ============================================================
// USB -> ingest pipeline
// ============================================================
static void pumpUsbToTarget() {
  uint8_t buf[64];
  size_t n = 0;
  while (Serial.available() && n < sizeof(buf)) {
    buf[n++] = (uint8_t)Serial.read();
  }
  if (n) ingestFromClient(Command::Source::USB, buf, n);
}

// ============================================================
// Arduino setup/loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(50);
  DebugRegistry::dump(Serial);
  delay(250);

  pinMode(PIN_TARGET_RESET, OUTPUT);
  pinMode(PIN_TARGET_FEL, OUTPUT);
  digitalWrite(PIN_TARGET_RESET, HIGH);
  digitalWrite(PIN_TARGET_FEL, HIGH);

  OTA::begin();
  OTA::markAppValidIfPending();   // rollback safety

  pinMode(PIN_LED, OUTPUT);
  led_set(false);

  loadUartConfig();
  loadApResetConfig();

  TargetSerial.begin(currentBaud, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  BlueprintRuntime::begin(TargetSerial, &Serial);

  gRestore.begin();
  if (gRestore.loadFromFile("/restore/manifest.json")) {
    DBG_PRINTF("[RESTORE] Manifest loaded: /restore/manifest.json\n");
  } else {
    DBG_PRINTF("[RESTORE] No manifest at /restore/manifest.json (OK)\n");
  }

  DBG_PRINTF("[BOOT] %s v%s\n", APP_NAME, APP_VERSION);
  DBG_PRINTF("[BOOT] Target UART RX=%d TX=%d baud=%lu auto=%d\n",
             PIN_UART_RX, PIN_UART_TX, (unsigned long)currentBaud, baudAuto ? 1 : 0);

  backupMgr.begin(&TargetSerial, &prefs);
  restoreMgr.begin(&TargetSerial);

  if (SdCache::begin()) DBG_PRINTF("[SD] mounted\n");
  else DBG_PRINTF("[SD] not mounted\n");

  // NEW: Mount LittleFS for CK2 storage
  if (LittleFS.begin(true)) {
    DBG_PRINTF("[LFS] mounted\n");
    if (!LittleFS.exists(CK2_FS_DIR)) LittleFS.mkdir(CK2_FS_DIR);
  } else {
    DBG_PRINTF("[LFS] not mounted\n");
  }

  bool ok = startSTAWithTimeout();
  if (!ok) startAP();

  SafeGuard::begin();

  setupCommandContext();

  setupWeb();
  startTcpServer();

  if (baudAuto) {
    uint32_t b = autodetectBaud();
    applyTargetBaud(b);
  }

  DBG_PRINTF("[BOOT] Ready.\n");
}

void loop() {
  if (apMode) dns.processNextRequest();

  if (apMode && noSsidAutoResetEnabled) {
    if (!haveSavedSsid()) {
      if (!apTimerArmed()) armApTimerNow();

      if (apElapsedMs() >= noSsidAutoResetAfterMs) {
        DBG_PRINTF("[WIFI] AP no-SSID timeout expired (%lu ms) -> reboot\n",
                   (unsigned long)noSsidAutoResetAfterMs);
        delay(150);
        ESP.restart();
      }
    } else {
      clearApTimer();
    }
  }

  // run Autobaud from loop()
  if (autoBaudRequested && !autoBaudRunning) {
    autoBaudRequested = false;
    autoBaudRunning = true;
    autoBaudStatus = "running";

    DBG_PRINTF("[AUTOBAUD] starting (loop)\n");

    uint32_t b = autodetectBaud();

    applyTargetBaud(b);
    baudAuto = false;
    saveUartConfig(false, b);

    autoBaudResult = b;
    autoBaudStatus = String("done: ") + b;

    DBG_PRINTF("[AUTOBAUD] done -> %lu\n", (unsigned long)b);

    autoBaudRunning = false;
  }

  pumpTargetToOutputs();

  SafeGuard::tick();
  BlueprintRuntime::tick();

  pumpUsbToTarget();

  backupMgr.tick();
  restoreMgr.tick();

  ws.cleanupClients();

  // NEW: hidden WS maintenance
  K2BUI::tick();

  delay(2);
}