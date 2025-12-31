#pragma once
#include "Debug.h"
#include <Arduino.h>

#include "BridgeState.h"
#include "WifiPortal.h"

class DNSServer;

namespace APcheck {

  // Call once in setup() AFTER you have Storage ready.
  // If SSID exists: tries STA (with timeout). If fail -> starts AP.
  // If NO SSID exists: starts AP immediately and arms timer.
  void begin(BridgeState& st, DNSServer& dns);

  // Call continuously in loop().
  // - Ensures AP timer is armed when in AP mode
  // - If no SSID saved: enforces AP mode
  // - If enabled + timer expired: reboot
  void tick(BridgeState& st, DNSServer& dns);

  // Helper: call this right after you start AP (or when you detect AP active)
  // Ensures the timer is "armed" even if millis() == 0.
  void armApTimer(BridgeState& st);

  // Optional manual reset trigger (for Web UI button)
  // Returns true if it actually restarted
  bool doManualResetNow();
}
