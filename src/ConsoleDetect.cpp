#include "ConsoleDetect.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static TargetConsoleState s_state = TargetConsoleState::Unknown;
static uint32_t s_lastSeenMs = 0;

static void setState(TargetConsoleState st, uint32_t nowMs) {
  if (s_state != st) {
    s_state = st;
  }
  s_lastSeenMs = nowMs;
}

void ConsoleDetect::begin() {
  s_state = TargetConsoleState::Unknown;
  s_lastSeenMs = 0;
}

void ConsoleDetect::onUbootPrompt(uint32_t nowMs) {
  setState(TargetConsoleState::UBoot, nowMs);
}

// Very light heuristics (keeps bridge fast & robust).
// Later, DeviceBlueprintLib can replace/augment this with richer detection.
void ConsoleDetect::onLine(const String& in, uint32_t nowMs) {
  String line = in;
  line.trim();
  if (!line.length()) return;

  // Login prompts
  if (line.endsWith("login:") || line.endsWith("login") || line.indexOf("k2 login") >= 0) {
    setState(TargetConsoleState::Login, nowMs);
    return;
  }
  if (line.startsWith("Password")) {
    setState(TargetConsoleState::Login, nowMs);
    return;
  }

  // Linux boot/log markers
  if (line.indexOf("Linux version") >= 0 || line.indexOf("BusyBox") >= 0 || line.indexOf("Starting kernel") >= 0) {
    // Not strictly "Linux prompt" yet, but we know we're beyond U-Boot.
    setState(TargetConsoleState::Unknown, nowMs);
    return;
  }

  // Shell prompt-ish lines.
  // Examples: "root@k2:~#" or just "#" at end of line.
  if (line.endsWith("#") || line.endsWith("# ") || line.indexOf("# ") >= 0) {
    setState(TargetConsoleState::Linux, nowMs);
    return;
  }

  // U-Boot env output often contains "=>" on a line by itself (handled by onUbootPrompt),
  // but some terminals echo it inside the line buffer.
  if (line == "=>" || line.endsWith("=>")) {
    setState(TargetConsoleState::UBoot, nowMs);
    return;
  }
}

TargetConsoleState ConsoleDetect::state() {
  return s_state;
}

const char* ConsoleDetect::stateName() {
  switch (s_state) {
    case TargetConsoleState::UBoot: return "uboot";
    case TargetConsoleState::Login: return "login";
    case TargetConsoleState::Linux: return "linux";
    default: return "unknown";
  }
}

uint32_t ConsoleDetect::lastSeenMs() {
  return s_lastSeenMs;
}

bool ConsoleDetect::fresh(uint32_t nowMs, uint32_t maxAgeMs) {
  if (s_lastSeenMs == 0) return false;
  return (nowMs - s_lastSeenMs) <= maxAgeMs;
}
