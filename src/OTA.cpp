#include "OTA.h"
#include "Debug.h"
#include "SdCache.h"

#include <Update.h>

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

void OTA::attach(AsyncWebServer& server) {

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