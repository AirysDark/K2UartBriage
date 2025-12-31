#pragma once
#include "Debug.h"
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>

#include "BridgeState.h"

// ============================================================
// WifiPortal
// - STA connect with timeout
// - AP fallback (captive portal)
// - Auto reset timer handling (delegates to BridgeState helpers)
// ============================================================

class WifiPortal {
public:
  static void bootBanner();

  // Start captive AP
  static void startAP(BridgeState& st, DNSServer& dns);

  // Attempt STA connect, returns true if connected
  static bool startSTAWithTimeout(BridgeState& st);

  // Call from loop() continuously
  static void tick(BridgeState& st);
};
