// ============================================================
// env_parse.h (FULL COPY/PASTE)
// ============================================================
#pragma once
#include "Debug.h"
#include <Arduino.h>

namespace EnvParse {

// Best-effort read of KEY=VALUE from a U-Boot printenv blob.
// Returns empty string if not found.
String get(const String& env, const char* key);

// Infer a stable board identifier from env text.
// Returns e.g. "chipid_..." or "serial_..." or "unknown_<hash>".
String inferBoardId(const String& env);

// Extract a lightweight JSON hint about layout/boot variables from env text.
// (NOT a GPT parser; just a safe, best-effort summary for the UI.)
String layoutHintJson(const String& env);

// Utility: sanitize to safe identifier charset.
String sanitizeId(const String& s);

} // namespace EnvParse
