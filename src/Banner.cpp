#include <Arduino.h>
#include "AppConfig.h"
#include "Debug.h"

void printBootBanner(const char* module, const char* msg)
{
  // Keep it tiny and safe (no heap allocations beyond Arduino String used internally by Debug)
  Debug::println(module ? module : "MAIN", Debug::Level::Info, "============================================================");
  if (msg && *msg) {
    Debug::println(module ? module : "MAIN", Debug::Level::Info, msg);
  }
  Debug::printf(module ? module : "MAIN", Debug::Level::Info,
                "%s v%s (%s)\n", APP_NAME, APP_VERSION, APP_BUILD);
  Debug::println(module ? module : "MAIN", Debug::Level::Info, "============================================================");
}
