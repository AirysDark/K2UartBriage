#include "BridgeState.h"
#include "Debug.h"
#include "AppConfig.h"

DBG_REGISTER_MODULE(__FILE__);

void BridgeState::markApStarted() {
  apStartedMs = millis();
}

void BridgeState::clearApTimer() {
  apStartedMs = 0;
}

bool BridgeState::apTimerArmed() const {
  return apStartedMs != 0;
}

uint32_t BridgeState::apElapsedMs() const {
  if (!apTimerArmed()) return 0;
  return millis() - apStartedMs;
}

bool BridgeState::apNoSsidTimeoutExpired() const {
  if (!noSsidAutoResetEnabled) return false;
  if (!apTimerArmed()) return false;
  return apElapsedMs() >= noSsidAutoResetAfterMs;
}

void BridgeState::clearLog() {
  logHead = 0;
}

void BridgeState::logAppend(const uint8_t* data, size_t len) {
  if (!data || !len) return;

  // Ring buffer write
  for (size_t i = 0; i < len; i++) {
    logbuf[logHead] = data[i];
    logHead = (logHead + 1) % CFG_LOGBUF_SIZE;
  }

  // Optional WS broadcast tap
  if (wsBroadcast) {
    wsBroadcast(data, len);
  }
}