#pragma once
#include "Debug.h"
#include <Arduino.h>
#include "Pins.h"

static inline void led_set(bool on) {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

static inline String ipToString(const IPAddress& ip) {
  return String(ip[0])+"."+String(ip[1])+"."+String(ip[2])+"."+String(ip[3]);
}
