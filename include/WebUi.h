// ===============================
// include/WebUi.h
// ===============================
#pragma once
#include "Debug.h"

#include <Arduino.h>
#include "BridgeState.h"

// forward declare (ESPAsyncWebServer.h defines this)
class AsyncWebServerRequest;

class WebUi {
public:
  static void bootBanner();
  static void begin(BridgeState& st);
  static void loop();

  static bool isCaptiveRequest(BridgeState& st, AsyncWebServerRequest* req);
};