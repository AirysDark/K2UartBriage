#pragma once
#include "Debug.h"
#include <Arduino.h>

// ============================================================
// pins.h (ESP32-S3 wiring + UART bridge pin mapping)
// Board: ESP32-S3-WROOM-1 (N16R8, 44-pin, USB-C)
// ============================================================
//
// Goals:
// - UART bridge uses Serial2 on safe GPIOs
// - Keep USB pins untouched (GPIO19/20 on many S3 boards)
// - Provide explicit RESET + FEL strap outputs
// - Provide LED pin (change if your board differs)
//
// IMPORTANT:
// - These are "safe defaults" for many 44-pin S3 dev boards,
//   but some vendors route the on-board LED differently.
// ============================================================


// ------------------------------------------------------------
// USB (reference only)
// ------------------------------------------------------------
// These are the common native USB pins on ESP32-S3.
// You *can* define them here for reference/logging/pin maps.
static const int PIN_USB_DM = 19;  // USB D-
static const int PIN_USB_DP = 20;  // USB D+


// ------------------------------------------------------------
// UART Bridge (Target <-> ESP32-S3)
// ------------------------------------------------------------
// Use Serial2 with explicit pin mapping.
// Wiring:
//   ESP32-S3 RX (PIN_UART_RX)  <- Target TX
//   ESP32-S3 TX (PIN_UART_TX)  -> Target RX
static const int PIN_UART_RX = 15;
static const int PIN_UART_TX = 16;


// ------------------------------------------------------------
// Target control pins (Active LOW)
// ------------------------------------------------------------
// Recommended: drive through transistor / open-drain style where possible.
// If direct GPIO: ensure target logic level is safe (3.3V).
static const int PIN_TARGET_RESET = 17; // Active LOW reset
static const int PIN_TARGET_FEL   = 18; // Active LOW FEL/BOOT strap


// ------------------------------------------------------------
// Status LED
// ------------------------------------------------------------
// Many ESP32-S3 dev boards use GPIO21 or GPIO2.
static const int PIN_LED = 21;


// ============================================================
// Helper: initialize UART bridge pins
// Call this from setup() BEFORE using Serial2
// ============================================================
static inline void uartBridgeBegin(uint32_t baud = 115200) {
  // ESP32-S3 Arduino core supports Serial2 with mapped pins
  Serial2.begin(baud, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
}


// ============================================================
// Helper: initialize target control pins
// Call this from setup()
// ============================================================
static inline void targetCtrlPinsBegin() {
  pinMode(PIN_TARGET_RESET, OUTPUT);
  pinMode(PIN_TARGET_FEL,   OUTPUT);

  // default = not asserted (Active LOW)
  digitalWrite(PIN_TARGET_RESET, HIGH);
  digitalWrite(PIN_TARGET_FEL,   HIGH);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
}
