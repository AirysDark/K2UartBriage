#include "WifiPortal.h"
#include "AppConfig.h"
#include "Debug.h"
#include "Storage.h"

#include <WiFi.h>

DBG_REGISTER_MODULE(__FILE__);

// ------------------------------------------------------------
// Simple LED helper (optional status blink)
// ------------------------------------------------------------
static void led(bool on) {
#if defined(PIN_LED)
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

// ------------------------------------------------------------
// Boot banner
// ------------------------------------------------------------
void WifiPortal::bootBanner() {
  printBootBanner("WIFI", "STA/AP + captive DNS");
}

// ------------------------------------------------------------
// Helper: check if SSID is stored
// ------------------------------------------------------------
static inline bool hasStoredSsid() {
  WifiCreds c = Storage::loadWifi();
  return c.has && c.ssid.length() > 0;
}

// ------------------------------------------------------------
// Start Access Point (captive portal)
// ------------------------------------------------------------
void WifiPortal::startAP(BridgeState& st, DNSServer& dns) {
  st.apMode = true;

  // Arm AP timer (make sure it can never be 0)
  st.apStartedMs = millis();
  if (st.apStartedMs == 0) st.apStartedMs = 1;

  // Clean slate
  WiFi.disconnect(true, true);
  delay(50);

  WiFi.mode(WIFI_AP);

  // Use AppConfig defaults
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  WiFi.softAP(CFG_WIFI_AP_SSID, CFG_WIFI_AP_PASS);

#if ENABLE_CAPTIVE_PORTAL
  dns.start(DNS_PORT, "*", AP_IP);
#endif

  D_WIFI("AP started ssid='%s' ip=%s\n", CFG_WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ------------------------------------------------------------
// Attempt STA connection with timeout
// ------------------------------------------------------------
bool WifiPortal::startSTAWithTimeout(BridgeState& st) {
  st.apMode = false;

  // Disarm AP timer whenever we try STA
  st.clearApTimer();

  WifiCreds c = Storage::loadWifi();
  if (!c.has || c.ssid.length() == 0) {
    D_WIFILN("No stored WiFi creds -> AP");
    return false;
  }

  WiFi.disconnect(true, true);
  delay(50);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(c.ssid.c_str(), c.pass.c_str());

  D_WIFI("STA connect start ssid='%s'\n", c.ssid.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    led(((millis() / 500) & 1) != 0);
  }
  led(false);

  if (WiFi.status() == WL_CONNECTED) {
    D_WIFI("Connected ip=%s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  D_WIFILN("STA connect timeout -> fallback AP");
  WiFi.disconnect(true, true);
  return false;
}

// ------------------------------------------------------------
// Tick (call from loop())
// - reboot ONLY if: AP mode AND no SSID saved AND timer expired
// ------------------------------------------------------------
void WifiPortal::tick(BridgeState& st) {
  if (!st.noSsidAutoResetEnabled) return;
  if (!st.apMode) return;

  // If creds exist, never auto-reset and also disarm timer.
  if (hasStoredSsid()) {
    st.clearApTimer();
    return;
  }

  // Ensure timer is armed
  if (!st.apTimerArmed()) st.markApStarted();

  // Expired?
  if (st.apNoSsidTimeoutExpired()) {
    D_WIFILN("AP no-SSID timeout expired -> reboot");
    delay(150);
    ESP.restart();
  }
}
