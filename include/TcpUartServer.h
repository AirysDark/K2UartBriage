#pragma once
#include "Debug.h"
#include <Arduino.h>
#include "BridgeState.h"

// TCP server that forwards raw bytes to the UART bridge.
// Single-client policy is enforced by BridgeState::tcpClient.
class TcpUartServer {
public:
  static void bootBanner();
  static void begin(BridgeState& st);
  static uint16_t port();
};
