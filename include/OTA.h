#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h> 
#include "Debug.h"

// ============================================================
// OTA
// - HTTP upload of firmware (.bin)
// - Streams directly to flash (Update.h)
// - Progress exposed for /api/status polling
// - Reboots automatically on success
// - Optional dual-partition rollback support
// ============================================================

namespace OTA {

void begin();

// Call in setup() AFTER your system is stable (WiFi/web/tcp running).
// If current running image is pending verify, this marks it valid
// and cancels rollback.
void markAppValidIfPending();

// Registers OTA upload endpoint:
//   POST /api/ota/upload (multipart/form-data)
//   file field name can be anything (browser typically uses "firmware")
void attach(AsyncWebServer& server);

// Status helpers for /api/status
bool inProgress();
uint32_t progressBytes();
uint32_t totalBytes();
const char* lastError();

} // namespace OTA