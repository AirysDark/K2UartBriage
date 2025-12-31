#pragma once
#include <Arduino.h>
#include "Debug.h"

// Thin wrapper layer for "device awareness" + scripts/prompts/gcode presets,
// without relying on unknown DeviceBlueprintLib APIs (except feedTargetChar which we KNOW exists).
namespace BlueprintRuntime {

  enum class Mode : uint8_t {
    Unknown = 0,
    Boot,
    UBoot,
    LinuxLoginUser,
    LinuxLoginPass,
    LinuxShell
  };

  // Must be called after TargetSerial.begin()
  // target = your UART stream to the printer/board
  // debug  = optional debug stream (Serial, etc)
  bool begin(Stream& target, Stream* debug = nullptr);

  // Feed raw bytes from target UART (call from the same place you already pump output).
  void feedBytes(const uint8_t* data, size_t len);

  // Feed a completed line (recommended if you already assemble lines).
  void feedLine(const String& line);

  // Poll any internal timers/logic (optional; safe to call often).
  void tick();

  // State access
  Mode mode();
  const String& lastLine();

  // "Keys" extracted (minimal for now)
  // - board_id (if seen)
  // - layout_json (if seen)
  String getKey(const String& k);
  String listKeysCsv();

  // Script system (reads /bp/scripts.json via ArduinoJson)
  String listScriptsCsv();
  bool runScript(const String& name, uint32_t timeoutMs = 4000);

  // Prompts system (reads /bp/prompts.json)
  // - list prompt names: "recovery_overview,ums_warning,..."
  // - get prompt text: returns multi-line string
  String listPromptsCsv();
  String getPromptText(const String& name);

  // Gcode/preset command system (reads /bp/gcode.json)
  // "groups": { "uboot": { "printenv": "printenv", ... }, "linux": {...} }
  String listGcodeGroupsCsv();
  String listGcodeNamesCsv(const String& group);
  String getGcodeLine(const String& group, const String& name);
  bool sendGcode(const String& group, const String& name);

  // Asset load status
  bool assetsLoaded();     // scripts.json ok
  bool promptsLoaded();    // prompts.json ok
  bool gcodeLoaded();      // gcode.json ok

} // namespace BlueprintRuntime