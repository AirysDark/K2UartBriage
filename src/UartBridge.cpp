#include "UartBridge.h"
#include "BridgeState.h"
#include "AppConfig.h"
#include "Pins.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static HardwareSerial TargetSerial(2);



static bool isPrintable(uint8_t b) {
  return (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20 && b <= 0x7E));
}

void UartBridge::bootBanner() {
  printBootBanner("UART", "Target UART + autobaud + target control");
}

HardwareSerial& UartBridge::serial() { return TargetSerial; }

void UartBridge::begin(BridgeState& st) {
  // Ensure we always start with a sane baud (prefer config default if unset/zero)
  if (st.currentBaud == 0) st.currentBaud = CFG_UART_DEFAULT_BAUD;

  TargetSerial.begin(st.currentBaud, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  D_UART("Target UART RX=%d TX=%d baud=%lu auto=%d\n",
         PIN_UART_RX, PIN_UART_TX,
         (unsigned long)st.currentBaud,
         st.baudAuto ? 1 : 0);
}

void UartBridge::applyBaud(BridgeState& st, uint32_t baud) {
  st.currentBaud = baud;
  TargetSerial.updateBaudRate(baud);
  D_UART("Baud set to %lu\n", (unsigned long)baud);
}

uint32_t UartBridge::autodetectBaud(BridgeState& st, uint32_t sampleMs) {
  // Candidate list stays the same (behavior preserved)
  const uint32_t candidates[] = {115200, 57600, 38400, 19200, 9600, 230400, 460800, 921600};

  uint32_t bestBaud = st.currentBaud ? st.currentBaud : CFG_UART_DEFAULT_BAUD;
  float bestScore = -1.0f;

  D_UARTLN("Autobaud scan start");

  for (uint32_t b : candidates) {
    TargetSerial.updateBaudRate(b);
    delay(50);

    uint32_t start = millis();
    size_t total = 0, printable = 0, zeros = 0;

    while (millis() - start < sampleMs) {
      while (TargetSerial.available()) {
        uint8_t c = (uint8_t)TargetSerial.read();
        total++;
        if (c == 0x00) zeros++;
        if (isPrintable(c)) printable++;

        // use project config instead of hardcoded 512 cap
        if (total >= CMD_LINEBUF_MAX) break;
      }
      if (total >= CMD_LINEBUF_MAX) break;
      delay(2);
    }

    float pr = total ? (float)printable / (float)total : 0.0f;
    float z  = total ? (float)zeros / (float)total : 0.0f;
    float bytesFactor = (float)min<size_t>(total, 256) / 256.0f;
    float score = (total < 16) ? -1.0f : (pr * bytesFactor - (z * 0.25f));

    D_UART("[AUTOBAUD] %lu total=%u pr=%.2f z=%.2f score=%.3f\n",
           (unsigned long)b, (unsigned)total, pr, z, score);

    if (score > bestScore) {
      bestScore = score;
      bestBaud = b;
    }
  }

  TargetSerial.updateBaudRate(bestBaud);
  D_UART("[AUTOBAUD] Selected %lu (score=%.3f)\n", (unsigned long)bestBaud, bestScore);
  return bestBaud;
}

void UartBridge::targetResetPulse(uint32_t ms) {
  digitalWrite(PIN_TARGET_RESET, LOW);
  delay(ms);
  digitalWrite(PIN_TARGET_RESET, HIGH);
  D_UART("Target reset pulse %ums\n", (unsigned)ms);
}

void UartBridge::targetEnterFEL() {
  digitalWrite(PIN_TARGET_FEL, LOW);
  delay(50);
  targetResetPulse(200);
  delay(600);
  digitalWrite(PIN_TARGET_FEL, HIGH);
  D_UARTLN("Target enter-FEL sequence sent");
}

void UartBridge::pumpTargetToOutputs(BridgeState& st) {
  // keep same behavior, but use IO_CHUNK_BYTES where sensible
  // (still capped to a small stack buffer so we don't blow stack)
  uint8_t buf[256];
  size_t n = 0;

  while (TargetSerial.available() && n < sizeof(buf)) {
    buf[n++] = (uint8_t)TargetSerial.read();
  }
  if (!n) return;

  for (size_t i = 0; i < n; i++) {
    st.logbuf[st.logHead] = buf[i];
    st.logHead = (st.logHead + 1) % CFG_LOGBUF_SIZE;
  }

  Serial.write(buf, n);

  if (st.tcpClient && st.tcpClient->connected()) {
    st.tcpClient->write((const char*)buf, n);
  }

  if (st.wsBroadcast) {
    st.wsBroadcast(buf, n);
  }
}

void UartBridge::pumpUsbToTarget() {
  while (Serial.available()) {
    uint8_t c = (uint8_t)Serial.read();
    TargetSerial.write(&c, 1);
  }
}
