#include "APcheck.h"

#include <DNSServer.h>

#include "Storage.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

// local helper: true only when a saved SSID is present
static inline bool hasSavedSsid() {
  WifiCreds c = Storage::loadWifi();
  return c.has && c.ssid.length() > 0;
}

// local helper: guarantee non-zero millis stamp
static inline uint32_t safeMillisStamp() {
  uint32_t m = millis();
  return (m == 0) ? 1 : m;
}

void APcheck::armApTimer(BridgeState& st) {
  st.apStartedMs = safeMillisStamp();
}

bool APcheck::doManualResetNow() {
  delay(150);
  ESP.restart();
  return true;
}

void APcheck::begin(BridgeState& st, DNSServer& dns) {
  // If there is no saved SSID -> AP immediately.
  if (!hasSavedSsid()) {
    D_WIFILN("[APcheck] No saved SSID -> start AP");
    WifiPortal::startAP(st, dns);
    armApTimer(st);
    return;
  }

  // Otherwise attempt STA first.
  if (WifiPortal::startSTAWithTimeout(st)) {
    D_WIFILN("[APcheck] STA connected");
    st.clearApTimer();   // ensure timer not armed in STA mode
    return;
  }

  // STA failed -> fallback AP
  D_WIFILN("[APcheck] STA failed -> start AP");
  WifiPortal::startAP(st, dns);
  armApTimer(st);
}

void APcheck::tick(BridgeState& st, DNSServer& dns) {
  // If AP mode is active, captive DNS needs pumping.
  // (Safe to call even if not started; it just does nothing.)
  dns.processNextRequest();

  // If SSID appears later (user saves creds) we don't force STA here.
  // Your existing "Save Wi-Fi + reboot" flow handles that.
  const bool saved = hasSavedSsid();

  // If no SSID saved -> enforce AP mode
  if (!saved) {
    if (!st.apMode) {
      D_WIFILN("[APcheck] No SSID + not AP -> start AP");
      WifiPortal::startAP(st, dns);
      armApTimer(st);
    }
  }

  // Timer enforcement only makes sense in AP mode, and only when NO SSID is saved
  if (!st.apMode) return;
  if (saved) return;

  if (!st.noSsidAutoResetEnabled) return;

  // Ensure timer is armed (millis() might have been 0 at first stamp)
  if (!st.apTimerArmed()) armApTimer(st);

  // Use your BridgeState helpers if you already implemented them
  // If you don't have apElapsedMs(), we compute it directly.
  uint32_t elapsed = (uint32_t)(millis() - st.apStartedMs);

  if (elapsed >= st.noSsidAutoResetAfterMs) {
    D_WIFILN("[APcheck] AP + no SSID timeout expired -> reboot");
    delay(200);
    ESP.restart();
  }
}