#include "AppConfig.h"
#include "Storage.h"
#include "Debug.h"
#include <Preferences.h>

DBG_REGISTER_MODULE(__FILE__);

static Preferences prefs;

void Storage::bootBanner() {
  printBootBanner("STORE", "Preferences (NVS) ready");
}

void Storage::saveWifi(const String& ssid, const String& pass) {
  prefs.begin("bridge", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  D_STORE("Saved WiFi ssid='%s' (pass len=%u)\n", ssid.c_str(), (unsigned)pass.length());
}

WifiCreds Storage::loadWifi() {
  WifiCreds c;
  prefs.begin("bridge", true);
  c.ssid = prefs.getString("ssid", "");
  c.pass = prefs.getString("pass", "");
  prefs.end();
  c.has = c.ssid.length() > 0;
  D_STORE("Load WiFi has=%d ssid='%s'\n", c.has ? 1 : 0, c.ssid.c_str());
  return c;
}

void Storage::clearWifi() {
  prefs.begin("bridge", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  D_STORELN("Cleared WiFi credentials");
}

void Storage::saveUart(bool autoBaud, uint32_t baud) {
  prefs.begin("bridge", false);
  prefs.putBool("baudAuto", autoBaud);
  prefs.putUInt("baud", baud);
  prefs.end();
  D_STORE("Saved UART auto=%d baud=%lu\n", autoBaud ? 1 : 0, (unsigned long)baud);
}

UartConfig Storage::loadUart() {
  UartConfig u;
  prefs.begin("bridge", true);
  u.autoBaud = prefs.getBool("baudAuto", true);
  u.baud = prefs.getUInt("baud", 115200);
  prefs.end();
  D_STORE("Load UART auto=%d baud=%lu\n", u.autoBaud ? 1 : 0, (unsigned long)u.baud);
  return u;
}
