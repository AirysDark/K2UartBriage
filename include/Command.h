#pragma once
#include "Debug.h"
#include <Arduino.h>

// ============================================================
// Command system
// - Any input LINE starting with '!' is treated as a local command
// - Everything else passes through to target UART normally
//
// This header defines ONLY the parser + the Context callbacks.
// main.cpp (or your router) wires Context.
// ============================================================

class Command {
public:
  enum class Source : uint8_t { USB = 0, WS = 1, TCP = 2 };

  // How the command system talks to your app (no globals inside Command.cpp)
  struct Context {
    // ---- output back to the caller ----
    void (*reply)(Source src, const char* msg) = nullptr;
    void (*replyLn)(Source src, const char* msg) = nullptr;

    // ---- target passthrough (REQUIRED if you want normal console to work) ----
    void (*targetWrite)(const uint8_t* data, size_t len) = nullptr;   // raw bytes to target
    void (*targetWriteLine)(const String& line) = nullptr;            // optional helper

    // ---- state queries ----
    bool      (*isApMode)() = nullptr;
    bool      (*haveSavedSsid)() = nullptr;
    uint32_t  (*apElapsedMs)() = nullptr;
    uint32_t  (*apTimerAfterMs)() = nullptr;
    bool      (*apTimerEnabled)() = nullptr;
    IPAddress (*ipNow)() = nullptr;

    // UART status
    uint32_t  (*uartGetBaud)() = nullptr;
    bool      (*uartGetAuto)() = nullptr;

    // OTA status
    bool      (*otaInProgress)() = nullptr;
    uint32_t  (*otaWritten)() = nullptr;
    uint32_t  (*otaTotal)() = nullptr;

    // SD status (return JSON string if you can)
    String    (*sdStatusJson)() = nullptr;

    // U-Boot/env status
    bool      (*ubootPromptFresh)() = nullptr;
    bool      (*umsIsActive)() = nullptr;
    String    (*envLastText)() = nullptr;
    String    (*envLastBoardId)() = nullptr;
    String    (*envLastLayoutJson)() = nullptr;

    // Backup status
    String    (*backupStatusLine)() = nullptr;
    float     (*backupProgress01)() = nullptr;
    String    (*backupGetProfileId)() = nullptr;
    void      (*backupGetCustomRange)(uint32_t& start, uint32_t& count) = nullptr;

    // Restore status
    String    (*restorePlan)() = nullptr;
    bool      (*restoreIsLoaded)() = nullptr;
    bool      (*restoreIsArmed)() = nullptr;

    // ---- actions: system ----
    void (*rebootNow)() = nullptr;

    // ---- actions: Wi-Fi ----
    void (*wifiSave)(const String& ssid, const String& pass) = nullptr;
    void (*wifiReset)() = nullptr;
    void (*forceApNow)() = nullptr;
    bool (*forceStaNow)() = nullptr; // return true if connected

    // AP timer controls
    void (*apTimerReset)() = nullptr;
    void (*apTimerSetAfterMs)(uint32_t ms) = nullptr;
    void (*apTimerSetEnabled)(bool en) = nullptr;

    // ---- actions: UART ----
    void (*uartSetBaud)(uint32_t baud) = nullptr;
    void (*uartSetAuto)(bool en) = nullptr;
    void (*uartRunAutodetectNow)() = nullptr;

    // ---- actions: Target ----
    void (*targetResetPulseMs)(uint32_t ms) = nullptr;
    void (*targetEnterFel)() = nullptr;

    // ---- actions: U-Boot / UMS / env ----
    void (*umsStart)() = nullptr;
    void (*umsClear)() = nullptr;
    void (*envCaptureStart)() = nullptr;

    // ---- actions: Backup / restore ----
    bool (*backupStartUart)() = nullptr;
    bool (*backupStartMeta)() = nullptr;
    void (*backupSetProfileId)(const String& pid) = nullptr;
    void (*backupSetCustomRange)(uint32_t start, uint32_t count) = nullptr;

    String (*restoreArm)(const String& token, bool overrideBoardId) = nullptr;
    void   (*restoreDisarm)() = nullptr;
    String (*restoreApply)() = nullptr;
    String (*restoreVerify)() = nullptr;

    // ---- SafeGuard hooks (optional) ----
    // Some builds wire these so UI / RPC layers can query or toggle unsafe mode.
    // Command.cpp itself calls SafeGuard directly.
    bool     (*sgIsUnsafe)() = nullptr;
    void     (*sgSetUnsafe)(bool on) = nullptr;
    uint32_t (*sgUnsafeRemainingMs)() = nullptr;
  };

  static void begin(Context* ctx);

  // Feed bytes from any source.
  // Returns true if bytes were consumed as part of commands.
  static bool feed(Source src, const uint8_t* data, size_t len);
  static bool feedText(Source src, const char* s);

private:
  static bool handleLine(Source src, const String& line);
  static String& buf(Source src);

  static void say(Source src, const String& s);
  static void sayLn(Source src, const String& s);
  static bool startsWithBang(const String& s);

  static void showHelp(Source src);

  static bool parseU32(const String& s, uint32_t& out);
  static bool parseBoolOnOff(const String& s, bool& out);
  static void splitFirst(const String& in, String& head, String& tail);
  static void splitTwo(const String& in, String& a, String& b);

  static bool isHexString(const String& s);
};