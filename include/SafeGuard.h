#pragma once
#include <Arduino.h>
#include "Debug.h"

namespace SafeGuard {

  void begin();
  void tick();

  // unsafe mode = allow blocked commands temporarily
  void setUnsafe(bool on);
  bool isUnsafe();

  // Remaining time until auto-disarm (0 if not unsafe)
  uint32_t unsafeRemainingMs();

  // Main policy gate:
  // - cmdHead = first token after '!' (e.g. "backup")
  // - cmdSub  = second token (e.g. "start")
  // - cmdArg  = rest
  // Returns true if command is allowed right now.
  bool allow(const String& cmdHead, const String& cmdSub, const String& cmdArg, String* whyBlocked = nullptr);
}