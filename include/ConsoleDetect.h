#pragma once
#include <Arduino.h>
#include "Debug.h"

// ============================================================
// Target console/prompt detection (bridge-level state)
// ============================================================

enum class TargetConsoleState : uint8_t {
  Unknown = 0,
  UBoot   = 1,
  Login   = 2,
  Linux   = 3,
};

namespace ConsoleDetect {
  // Call once at boot.
  void begin();

  // Notify detector that the U-Boot prompt ("=>") was observed.
  void onUbootPrompt(uint32_t nowMs);

  // Feed a *completed* printable line (no CR/LF). Best-effort heuristics.
  void onLine(const String& line, uint32_t nowMs);

  // Current detected state.
  TargetConsoleState state();
  const char* stateName();

  // For UI: "fresh" means we've seen a prompt/log marker recently.
  uint32_t lastSeenMs();
  bool fresh(uint32_t nowMs, uint32_t maxAgeMs = 5000);
}
