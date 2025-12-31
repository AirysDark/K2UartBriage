#pragma once
#include "Debug.h"
#include <Arduino.h>
#include "BridgeState.h"

class UartBridge {
public:
  static void bootBanner();

  static void begin(BridgeState& st);
  static void applyBaud(BridgeState& st, uint32_t baud);
  static uint32_t autodetectBaud(BridgeState& st, uint32_t sampleMs = 700);

  static void targetResetPulse(uint32_t ms = 200);
  static void targetEnterFEL();

  static void pumpTargetToOutputs(BridgeState& st);
  static void pumpUsbToTarget();

  static HardwareSerial& serial();
};
