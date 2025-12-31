#pragma once
#include <Arduino.h>
#include "AppConfig.h"
#include "Debug.h"
#include <AsyncTCP.h>

// ============================================================
// BridgeState
// Runtime state shared across WiFi, Web UI, UART, TCP bridge
// ============================================================

struct BridgeState {

  // ----------------------------------------------------------
  // Wi-Fi / AP state
  // ----------------------------------------------------------
  bool apMode = false;

  // Timestamp when AP mode started (millis)
  uint32_t apStartedMs = 0;

  // Safety: auto reboot if AP runs too long with NO stored SSID
  bool     noSsidAutoResetEnabled = true;
  uint32_t noSsidAutoResetAfterMs = 5UL * 60UL * 1000UL;

  // ----------------------------------------------------------
  // UART configuration
  // ----------------------------------------------------------
  bool     baudAuto     = true;
  uint32_t currentBaud  = 115200;

  // ----------------------------------------------------------
  // TX policy
  // ----------------------------------------------------------
  bool tcpExclusiveTx = true;
  bool webTxEnabled   = false;

  // Active TCP client (RAW / Telnet bridge)
  AsyncClient* tcpClient = nullptr;

  // ----------------------------------------------------------
  // WebSocket / log buffer
  // ----------------------------------------------------------
  uint8_t logbuf[CFG_LOGBUF_SIZE]{};
  size_t  logHead = 0;

  // Broadcast raw bytes to all WebSocket clients
  void (*wsBroadcast)(const uint8_t* data, size_t len) = nullptr;

  // ----------------------------------------------------------
  // Helpers
  // ----------------------------------------------------------
  void markApStarted();
  void clearApTimer();
  bool apTimerArmed() const;

  // Returns elapsed ms since AP started (0 if not armed)
  uint32_t apElapsedMs() const;

  // True if timer expired (and enabled)
  bool apNoSsidTimeoutExpired() const;

  // Clears log buffer state
  void clearLog();

  // Append to log buffer (ring buffer)
  void logAppend(const uint8_t* data, size_t len);
};
