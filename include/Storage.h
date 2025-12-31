#pragma once
#include "Debug.h"
#include <Arduino.h>

struct WifiCreds {
  String ssid;
  String pass;
  bool has = false;
};

struct UartConfig {
  bool autoBaud = true;
  uint32_t baud = 115200;
};

class Storage {
public:
  static void bootBanner();

  static void saveWifi(const String& ssid, const String& pass);
  static WifiCreds loadWifi();
  static void clearWifi();

  static void saveUart(bool autoBaud, uint32_t baud);
  static UartConfig loadUart();
};
