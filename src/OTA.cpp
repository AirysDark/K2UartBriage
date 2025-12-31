#include "OTA.h"
#include "Debug.h"
#include "SdCache.h"
#include <LittleFS.h>
#include <FS.h>
#include <memory>

#include <Update.h>

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

DBG_REGISTER_MODULE(__FILE__);

#if defined(ESP32)
  #include <esp_ota_ops.h>
  #include <esp_system.h>
#endif

// -----------------------------------------------------------

static bool     g_active   = false;
static uint32_t g_written  = 0;
static uint32_t g_total    = 0;
static String   g_lastErr;

// ============================================================
// Online update (GitHub Releases)
// ============================================================
// Repo: AirysDark/K2UartBriage (branch: main)
// Latest release must include an asset named: update.zip
// (This is the streamed dual-image container described below.)
//
// We stream the asset directly into zipWrite() (no temp storage).
// For reliability, we use WiFiClientSecure::setInsecure().

static bool     g_onlineActive = false;
static uint32_t g_onlineTotal  = 0;
static uint32_t g_onlineDone   = 0;
static String   g_onlinePhase  = "idle"; // idle/checking/downloading/flashing/done/error
static String   g_onlineMsg;
static String   g_onlineTag;
static String   g_onlineUrl;

static TaskHandle_t g_onlineTask = nullptr;

struct OnlineJob {
  String url;
  uint32_t size = 0;
};

static void onlineReset() {
  g_onlineActive = false;
  g_onlineTotal  = 0;
  g_onlineDone   = 0;
  g_onlinePhase  = "idle";
  g_onlineMsg    = "";
  g_onlineTag    = "";
  g_onlineUrl    = "";
}

static void onlineFail(const String& msg) {
  g_onlineMsg = msg;
  g_onlinePhase = "error";
  g_onlineActive = false;
  g_lastErr = msg;
}

static bool githubFetchLatest(String& outTag, String& outAssetUrl, uint32_t& outSize) {
  outTag = "";
  outAssetUrl = "";
  outSize = 0;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  const char* api = "https://api.github.com/repos/AirysDark/K2UartBriage/releases/latest";
  if (!http.begin(client, api)) {
    onlineFail("HTTP begin failed");
    return false;
  }
  http.addHeader("User-Agent", "K2UartBriage");
  http.addHeader("Accept", "application/vnd.github+json");

  const int code = http.GET();
  if (code != 200) {
    onlineFail(String("GitHub API failed: HTTP ") + code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    onlineFail("GitHub JSON parse failed");
    return false;
  }

  outTag = doc["tag_name"].as<String>();
  JsonArray assets = doc["assets"].as<JsonArray>();
  for (JsonObject a : assets) {
    const String name = a["name"].as<String>();
    if (name == "update.zip") {
      outAssetUrl = a["browser_download_url"].as<String>();
      outSize = a["size"].as<uint32_t>();
      break;
    }
  }

  if (outAssetUrl.length() == 0) {
    onlineFail("No update.zip asset found in latest release");
    return false;
  }
  return true;
}

static bool githubStreamAndFlash(const String& assetUrl, uint32_t expectedSize);

static void onlineTaskFn(void* pv) {
  OnlineJob* job = (OnlineJob*)pv;
  if (!job) {
    onlineFail("Online job missing");
    g_onlineTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // Run update (githubStreamAndFlash will reboot on success)
  githubStreamAndFlash(job->url, job->size);

  // If we reach here, it failed (no reboot)
  delete job;
  g_onlineTask = nullptr;
  vTaskDelete(nullptr);
}

// ============================================================
// "update.zip" (streamed container) support
// ============================================================
// To keep this project lightweight and stable, we implement a
// streaming dual-image update format that is uploaded as
// "update.zip" (file extension only).
//
// Container layout (little-endian):
//   8 bytes  magic  = "K2UPD1\0\0"
//   4 bytes  fw_size (uint32)
//   4 bytes  fs_size (uint32)
//   fw_size  bytes  firmware image (OTA app)
//   fs_size  bytes  littlefs image (filesystem)
//
// This lets us flash firmware.bin then littlefs.bin WITHOUT
// temporary storage or a real zip/unzip library.


// ============================================================
// Resumable upload session for update.zip (chunked upload)
// Stores to LittleFS then flashes from file.
// ============================================================
struct OtaUpSession {
  bool    active = false;
  String  id;
  String  path;
  uint32_t total = 0;
  uint32_t have  = 0;
  uint32_t lastMs = 0;
};
static OtaUpSession g_up;

static String makeSessionId() {
  uint32_t r = (uint32_t)esp_random();
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)r);
  return String(buf);
}

static void upReset() {
  g_up = OtaUpSession();
}

static bool     g_zipActive   = false;
static uint32_t g_zipFwSize   = 0;
static uint32_t g_zipFsSize   = 0;
static uint32_t g_zipFwW      = 0;
static uint32_t g_zipFsW      = 0;
static uint8_t  g_zipHdr[16];
static uint32_t g_zipHdrHave  = 0;
static uint8_t  g_zipStage    = 0; // 0=hdr, 1=fw, 2=fs

static inline uint32_t readLE32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void zipReset() {
  g_zipActive  = false;
  g_zipFwSize  = 0;
  g_zipFsSize  = 0;
  g_zipFwW     = 0;
  g_zipFsW     = 0;
  g_zipHdrHave = 0;
  g_zipStage   = 0;
}

bool OTA::inProgress() { return g_active; }
uint32_t OTA::progressBytes() { return g_written; }
uint32_t OTA::totalBytes() { return g_total; }
const char* OTA::lastError() { return g_lastErr.c_str(); }

void OTA::begin() {
  g_active  = false;
  g_written = 0;
  g_total   = 0;
  g_lastErr = "";
}

// ============================================================
// Rollback support (dual-partition)
// - If current running image is in PENDING_VERIFY, mark it VALID
// - This cancels rollback
// ============================================================
void OTA::markAppValidIfPending() {
#if defined(ESP32)
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    D_OTA("No running partition?");
    return;
  }

  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  esp_err_t e = esp_ota_get_state_partition(running, &st);
  if (e != ESP_OK) {
    D_OTA("esp_ota_get_state_partition failed: %d", (int)e);
    return;
  }

  if (st == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      D_OTA("Rollback cancelled: app marked VALID.");
    } else {
      D_OTA("Failed to mark app valid: %d", (int)err);
    }
  }
#endif
}

static inline void otaFail(const char* msg) {
  g_lastErr = msg ? msg : "OTA failed";
  D_OTA("%s", g_lastErr.c_str());
}

static inline void zipFail(const char* msg) {
  otaFail(msg);
  Update.abort();
  zipReset();
  g_active = false;
}

static bool zipBeginFromHeader() {
  static const uint8_t MAGIC[8] = { 'K','2','U','P','D','1',0,0 };
  if (memcmp(g_zipHdr, MAGIC, 8) != 0) {
    zipFail("Bad update.zip header (magic)");
    return false;
  }
  g_zipFwSize = readLE32(g_zipHdr + 8);
  g_zipFsSize = readLE32(g_zipHdr + 12);
  if (g_zipFwSize == 0 || g_zipFsSize == 0) {
    zipFail("Bad update.zip header (size=0)");
    return false;
  }

  // Start flashing firmware image
  if (!Update.begin((size_t)g_zipFwSize, U_FLASH)) {
    zipFail("Update.begin firmware failed");
    return false;
  }
  g_zipStage = 1;
  return true;
}

static bool zipWrite(const uint8_t* data, size_t len) {
  size_t off = 0;

  while (off < len) {
    // Header stage
    if (g_zipStage == 0) {
      const size_t need = 16 - g_zipHdrHave;
      const size_t take = (len - off < need) ? (len - off) : need;
      memcpy(g_zipHdr + g_zipHdrHave, data + off, take);
      g_zipHdrHave += (uint32_t)take;
      off += take;
      if (g_zipHdrHave == 16) {
        if (!zipBeginFromHeader()) return false;
      }
      continue;
    }

    // Firmware stage
    if (g_zipStage == 1) {
      const uint32_t remain = g_zipFwSize - g_zipFwW;
      const size_t take = (uint32_t)(len - off) > remain ? (size_t)remain : (len - off);
      if (take) {
        size_t w = Update.write((uint8_t*)(data + off), take);
        g_zipFwW += (uint32_t)w;
        g_written += (uint32_t)w;
        if (w != take) {
          zipFail("Update.write firmware failed");
          return false;
        }
        off += take;
      }
      if (g_zipFwW == g_zipFwSize) {
        if (!Update.end(true)) {
          zipFail("Update.end firmware failed");
          return false;
        }
        // Next: filesystem image
        if (!Update.begin((size_t)g_zipFsSize, U_SPIFFS)) {
          zipFail("Update.begin littlefs failed");
          return false;
        }
        g_zipStage = 2;
      }
      continue;
    }

    // Filesystem stage
    if (g_zipStage == 2) {
      const uint32_t remain = g_zipFsSize - g_zipFsW;
      const size_t take = (uint32_t)(len - off) > remain ? (size_t)remain : (len - off);
      if (take) {
        size_t w = Update.write((uint8_t*)(data + off), take);
        g_zipFsW += (uint32_t)w;
        g_written += (uint32_t)w;
        if (w != take) {
          zipFail("Update.write littlefs failed");
          return false;
        }
        off += take;
      }
      if (g_zipFsW == g_zipFsSize) {
        if (!Update.end(true)) {
          zipFail("Update.end littlefs failed");
          return false;
        }
        // Finished
        g_zipStage = 3;
      }
      continue;
    }

    // Done
    break;
  }
  return true;
}

// ============================================================
// Online update streamer (uses same zipWrite() dual-image parser)
// ============================================================
static bool githubStreamAndFlash(const String& assetUrl, uint32_t expectedSize) {
  // Try HTTPClient first (fast path)
  {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (http.begin(client, assetUrl)) {
      http.addHeader("User-Agent", "K2UartBriage");
      const int code = http.GET();
      if (code == 200) {
        WiFiClient* stream = http.getStreamPtr();

        g_onlineTotal = (expectedSize > 0) ? expectedSize : (uint32_t)http.getSize();
        g_onlineDone = 0;

        // Prepare ZIP parser
        zipReset();
        g_zipActive = true;
        g_zipStage  = 0;

        g_onlinePhase = "flashing";
        g_onlineActive = true;

        uint8_t buf[2048];
        uint32_t lastYield = millis();
        while (http.connected()) {
          const size_t avail = stream->available();
          if (avail) {
            size_t toRead = avail;
            if (toRead > sizeof(buf)) toRead = sizeof(buf);
            int got = stream->readBytes(buf, toRead);
            if (got > 0) {
              g_onlineDone += (uint32_t)got;
              if (!zipWrite(buf, (size_t)got)) {
                http.end();
                onlineFail(g_lastErr.length() ? g_lastErr : "ZIP flash failed");
                return false;
              }
            }
          } else {
            delay(1);
          }

          if (millis() - lastYield > 50) {
            lastYield = millis();
            delay(0);
          }
        }

        http.end();
        onlineFail("Asset stream ended unexpectedly");
        return false;
      } else {
        http.end();
        // fall through to manual downloader with better diagnostics
      }
    }
  }

  // Manual HTTPS downloader with redirect support (more robust)
  g_onlinePhase = "downloading";
  HttpBodyStream hs;
  if (!httpGetStreamFollowRedirect(assetUrl, hs, 6)) {
    // onlineFail already set
    return false;
  }
  if (hs.status != 200) {
    onlineFail(String("Asset HTTP status: ") + hs.status);
    return false;
  }

  g_onlineTotal = (expectedSize > 0) ? expectedSize : (uint32_t)((hs.contentLength > 0) ? hs.contentLength : 0);
  g_onlineDone = 0;

  zipReset();
  g_zipActive = true;
  g_zipStage  = 0;

  g_onlinePhase = "flashing";
  g_onlineActive = true;

  uint8_t buf[2048];
  uint32_t last = millis();
  while (hs.client && hs.client->connected()) {
    int avail = hs.client->available();
    if (avail > 0) {
      int toRead = avail;
      if (toRead > (int)sizeof(buf)) toRead = (int)sizeof(buf);
      int got = hs.client->readBytes((char*)buf, toRead);
      if (got > 0) {
        g_onlineDone += (uint32_t)got;
        if (!zipWrite(buf, (size_t)got)) {
          onlineFail(g_lastErr.length() ? g_lastErr : "ZIP flash failed");
          return false;
        }
      }
    } else {
      delay(1);
    }
    if (millis() - last > 50) { last = millis(); delay(0); }
  }

  onlineFail("Asset stream ended unexpectedly");
  return false;
}

void OTA::attach(AsyncWebServer& server) {

  // ============================================================
  // Online update (GitHub Releases)
  // ============================================================

  // Check latest release + asset url
  server.on("/api/ota/github_check", HTTP_GET, [](AsyncWebServerRequest* req) {
    String tag, url;
    uint32_t size = 0;
    g_onlinePhase = "checking";
    g_onlineMsg = "";

    const bool ok = githubFetchLatest(tag, url, size);
    if (ok) {
      g_onlineTag = tag;
      g_onlineUrl = url;
      g_onlineTotal = size;
      g_onlineDone = 0;
      g_onlinePhase = "idle";
    }

    JsonDocument doc;
    doc["ok"] = ok;
    doc["tag"] = g_onlineTag;
    doc["size"] = g_onlineTotal;
    doc["phase"] = g_onlinePhase;
    doc["msg"] = ok ? "" : g_onlineMsg;

    String out;
    serializeJson(doc, out);
    req->send(ok ? 200 : 500, "application/json", out);
  });

  // Start online update (requires UI confirmation)
  server.on("/api/ota/github_update", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (g_onlineTask) {
      req->send(409, "text/plain", "Online update already running");
      return;
    }
    if (g_onlineUrl.length() == 0) {
      req->send(400, "text/plain", "No cached release. Call /api/ota/github_check first.");
      return;
    }
    onlineReset();
    g_onlineTag = g_onlineTag.length() ? g_onlineTag : "latest";
    g_onlineUrl = g_onlineUrl;
    g_onlineTotal = g_onlineTotal;
    g_onlinePhase = "starting";
    g_onlineMsg = "";

    OnlineJob* job = new OnlineJob();
    job->url = g_onlineUrl;
    job->size = g_onlineTotal;

    // Respond immediately to avoid browser timeout
    req->send(200, "text/plain", String("Online update started (tag=") + g_onlineTag + ")");

    xTaskCreatePinnedToCore(onlineTaskFn, "ota_online", 8192, job, 1, &g_onlineTask, 1);
  });

  // Progress polling (UI)
  server.on("/api/ota/github_progress", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["active"] = (g_onlineTask != nullptr);
    doc["phase"] = g_onlinePhase;
    doc["tag"] = g_onlineTag;
    doc["done"] = g_onlineDone;
    doc["total"] = g_onlineTotal;
    doc["msg"] = g_onlineMsg;
    if (g_onlineTotal > 0) {
      doc["pct"] = (uint32_t)((g_onlineDone * 100ULL) / g_onlineTotal);
    } else {
      doc["pct"] = 0;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ============================================================
  // Dual-image streamed update container (uploaded as update.zip)
  // ============================================================
  server.on(
    "/api/ota/updatezip",
    HTTP_POST,

    // ===== Request finished =====
    [](AsyncWebServerRequest* req) {
      if (!Update.hasError() && g_zipStage == 3) {
        req->send(200, "text/plain", "OTA ZIP OK (fw + littlefs). Rebooting...");
        delay(200);
        ESP.restart();
      } else {
        req->send(500, "text/plain", String("OTA ZIP failed: ") + OTA::lastError());
      }
    },

    // ===== Upload handler =====
    [](AsyncWebServerRequest* req,
       const String& filename,
       size_t index,
       uint8_t* data,
       size_t len,
       bool final) {

      if (index == 0) {
        D_OTA("Upload ZIP start: %s", filename.c_str());

        g_active  = true;
        g_written = 0;
        g_total   = 0;
        g_lastErr = "";
        zipReset();

        const uint64_t contentLen = req->contentLength();
        if (contentLen == 0) {
          zipFail("Empty upload (content-length=0)");
          return;
        }
        g_total = (contentLen > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)contentLen;

        if (!filename.endsWith(".zip")) {
          zipFail("Rejected: filename not .zip");
          return;
        }
        g_zipActive = true;
        g_zipStage  = 0;
      }

      if (len && g_zipActive) {
        if (!zipWrite(data, len)) {
          // zipWrite already set error + aborted
          return;
        }
      }

      if (final) {
        if (g_zipStage != 3) {
          zipFail("Update ZIP incomplete (unexpected EOF)");
        } else {
          D_OTA("OTA ZIP success (fw=%u, fs=%u)", (unsigned)g_zipFwSize, (unsigned)g_zipFsSize);

          // SD firmware cache becomes stale once we flash
          if (SdCache::mounted()) {
            SdCache::remove(SdItem::Firmware);
          }
        }
        g_active = false;
        g_zipActive = false;
      }
    }
  );

  // ============================================================
  // Resumable chunked upload (stages update.zip to LittleFS)
  // ============================================================

  // Start / resume a session
  server.on("/api/ota/session/start", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!LittleFS.begin(true)) {
      req->send(500, "text/plain", "LittleFS not available");
      return;
    }

    if (!req->hasParam("size", true)) {
      req->send(400, "text/plain", "Missing size");
      return;
    }

    const uint32_t total = (uint32_t)req->getParam("size", true)->value().toInt();
    if (total == 0) {
      req->send(400, "text/plain", "Bad size");
      return;
    }

    // Reuse existing staged file if it matches total size expectation (resume)
    const String path = "/_tmp_update.zip";
    uint32_t have = 0;
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      if (f) { have = (uint32_t)f.size(); f.close(); }
      if (have > total) {
        LittleFS.remove(path);
        have = 0;
      }
    }

    g_up.active = true;
    g_up.id = makeSessionId();
    g_up.path = path;
    g_up.total = total;
    g_up.have = have;
    g_up.lastMs = millis();

    JsonDocument doc;
    doc["id"] = g_up.id;
    doc["total"] = g_up.total;
    doc["have"] = g_up.have;
    doc["chunk"] = 65536;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Status poll
  server.on("/api/ota/session/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["active"] = g_up.active;
    doc["id"] = g_up.id;
    doc["total"] = g_up.total;
    doc["have"] = g_up.have;
    doc["err"] = g_lastErr;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Upload one chunk (raw body)
  server.on("/api/ota/session/chunk", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(200, "text/plain", "OK");
    },
    NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!g_up.active) {
        g_lastErr = "No active session";
        return;
      }
      if (!req->hasParam("id") || !req->hasParam("offset")) {
        g_lastErr = "Missing id/offset";
        return;
      }
      const String id = req->getParam("id")->value();
      const uint32_t off = (uint32_t)req->getParam("offset")->value().toInt();
      if (id != g_up.id) {
        g_lastErr = "Session mismatch";
        return;
      }

      // Only accept the next expected offset
      if (off != g_up.have) {
        // mismatch - let client re-sync by polling status
        return;
      }

      if (!LittleFS.begin(true)) {
        g_lastErr = "LittleFS not mounted";
        return;
      }

      // Ensure file exists up to current size
      File f = LittleFS.open(g_up.path, LittleFS.exists(g_up.path) ? "r+" : "w+");
      if (!f) {
        g_lastErr = "Open staged file failed";
        return;
      }

      if (!f.seek(off, SeekSet)) {
        f.close();
        g_lastErr = "Seek failed";
        return;
      }

      size_t w = f.write(data, len);
      f.close();
      if (w != len) {
        g_lastErr = "Write failed";
        return;
      }

      g_up.have += (uint32_t)len;
      g_up.lastMs = millis();
    }
  );

  // Finalize + flash from staged file
  server.on("/api/ota/session/finalize", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!g_up.active) {
      req->send(400, "text/plain", "No active session");
      return;
    }
    if (g_up.have != g_up.total) {
      req->send(400, "text/plain", String("Incomplete: have=") + g_up.have + " total=" + g_up.total);
      return;
    }

    req->send(200, "text/plain", "Staged OK. Flashing...");

    // Flash in a task so the HTTP response can complete
    struct FlashJob { String path; };
    FlashJob* job = new FlashJob();
    job->path = g_up.path;

    xTaskCreatePinnedToCore([](void* pv){
      FlashJob* j = (FlashJob*)pv;
      if (j) {
        g_lastErr = "";
        flashUpdateZipFromFile(j->path);
        // On success, zipWrite() should reboot, so reaching here means failure.
        delete j;
      }
      vTaskDelete(nullptr);
    }, "ota_flash_file", 8192, job, 1, nullptr, 1);
  });



  server.on(
    "/api/ota/upload",
    HTTP_POST,

    // ===== Request finished =====
    [](AsyncWebServerRequest* req) {
      if (!Update.hasError()) {
        req->send(200, "text/plain", "OTA OK. Rebooting...");
        delay(200);
        ESP.restart();
      } else {
        req->send(500, "text/plain", String("OTA failed: ") + OTA::lastError());
      }
    },

    // ===== Upload handler =====
    [](AsyncWebServerRequest* req,
       const String& filename,
       size_t index,
       uint8_t* data,
       size_t len,
       bool final) {

      // Start
      if (index == 0) {
        D_OTA("Upload start: %s", filename.c_str());

        g_active  = true;
        g_written = 0;
        g_total   = 0;
        g_lastErr = "";

        const uint64_t contentLen = req->contentLength();
        if (contentLen == 0) {
          otaFail("Empty upload (content-length=0)");
          Update.abort();
          g_active = false;
          return;
        }

        // Keep total as uint32 for /api/status (UI progress bar)
        g_total = (contentLen > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)contentLen;

        // Basic filename check
        if (!filename.endsWith(".bin")) {
          otaFail("Rejected: filename not .bin");
          Update.abort();
          g_active = false;
          return;
        }

        // Begin Update (writes to OTA partition)
        if (!Update.begin((size_t)g_total)) {
          otaFail("Update.begin failed (not enough space / bad partition)");
          g_active = false;
          return;
        }
      }

      // Data chunk
      if (len) {
        size_t w = Update.write(data, len);
        g_written += (uint32_t)w;

        if (w != len) {
          otaFail("Update.write failed");
          Update.abort();
          g_active = false;
          return;
        }
      }

      // Finalize
      if (final) {
        if (Update.end(true)) {
          D_OTA("OTA success (%u bytes)", (unsigned)g_written);

          // SD firmware cache becomes stale once we flash
          if (SdCache::mounted()) {
            SdCache::remove(SdItem::Firmware);
          }

          // IMPORTANT:
          // Do NOT mark app valid here.
          // Call OTA::markAppValidIfPending() on next boot once stable.
        } else {
          otaFail("Update.end failed (bad image / checksum)");
        }

        g_active = false;
      }
    }
  );
}

// ============================================================
// Flash dual-image update container from a file on LittleFS
// ============================================================
static bool flashUpdateZipFromFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    g_lastErr = "Cannot open staged update.zip";
    return false;
  }

  zipReset();
  g_zipActive = true;
  g_zipStage  = 0;

  uint8_t buf[2048];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    if (!zipWrite(buf, n)) {
      f.close();
      return false;
    }
    delay(0);
  }
  f.close();

  // zipWrite() performs Update.end() and triggers reboot on success of both stages
  // If we reached here without reboot, something failed.
  return (g_lastErr.length() == 0);
}



// ============================================================
// Minimal HTTPS GET with redirect following (no HTTPClient begin issues)
// Returns connected client positioned at body.
// ============================================================
struct HttpBodyStream {
  std::unique_ptr<WiFiClientSecure> client;
  int      status = -1;
  int32_t  contentLength = -1;
};

static bool parseUrl(const String& url, String& host, String& path) {
  host = "";
  path = "/";
  if (!url.startsWith("https://")) return false;
  int p = url.indexOf('/', 8);
  if (p < 0) {
    host = url.substring(8);
    path = "/";
  } else {
    host = url.substring(8, p);
    path = url.substring(p);
  }
  return host.length() > 0;
}

static bool readLine(Client& c, String& out, uint32_t timeoutMs=8000) {
  out = "";
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (c.available()) {
      char ch = (char)c.read();
      if (ch == '\r') continue;
      if (ch == '\n') return true;
      out += ch;
      if (out.length() > 2048) return true;
    }
    delay(1);
  }
  return false;
}

static bool httpGetStreamFollowRedirect(const String& url, HttpBodyStream& out, int maxRedirect=5) {
  String cur = url;
  for (int i=0;i<=maxRedirect;i++) {
    String host, path;
    if (!parseUrl(cur, host, path)) {
      onlineFail("Bad asset URL");
      return false;
    }

    auto cli = std::make_unique<WiFiClientSecure>();
    cli->setInsecure();

    if (!cli->connect(host.c_str(), 443)) {
      onlineFail(String("HTTPS connect failed: ") + host);
      return false;
    }

    // Request
    cli->print(String("GET ") + path + " HTTP/1.1\r\n");
    cli->print(String("Host: ") + host + "\r\n");
    cli->print("User-Agent: K2UartBriage\r\n");
    cli->print("Accept: application/octet-stream\r\n");
    cli->print("Connection: close\r\n\r\n");

    // Status line
    String line;
    if (!readLine(*cli, line)) {
      onlineFail("HTTP read timeout");
      return false;
    }
    int status = -1;
    if (line.startsWith("HTTP/")) {
      int sp = line.indexOf(' ');
      if (sp > 0) status = line.substring(sp+1).toInt();
    }

    int32_t clen = -1;
    String location;

    // Headers
    while (readLine(*cli, line)) {
      if (line.length() == 0) break;
      String l = line;
      l.toLowerCase();
      if (l.startsWith("content-length:")) {
        clen = line.substring(line.indexOf(':')+1).toInt();
      } else if (l.startsWith("location:")) {
        location = line.substring(line.indexOf(':')+1);
        location.trim();
      }
    }

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      if (location.length() == 0) {
        onlineFail("Redirect with no Location");
        return false;
      }
      // Handle relative redirects
      if (location.startsWith("/")) {
        location = String("https://") + host + location;
      }
      cur = location;
      continue;
    }

    out.client = std::move(cli);
    out.status = status;
    out.contentLength = clen;
    return true;
  }
  onlineFail("Too many redirects");
  return false;
}

