#pragma once
#include <Arduino.h>
#include <FS.h>     // fs::FS
#include <stdarg.h>

// ============================================================
// Debug / Logging core for K2UartBriage (ESP32-S3 / Arduino)
// - Per-module log levels
// - Fan-out to: Serial, optional mirror stream, optional SD file
// - Ring-buffer lines for WebUI live stream
// ============================================================

namespace Debug {

  enum class Level : uint8_t {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
    Trace = 4
  };

  struct Config {
    bool enabled = true;

    // sinks
    Stream* primary = nullptr;     // usually &Serial
    Stream* mirror  = nullptr;     // optional (UART/TCP bridge etc.)
    fs::FS* sd_fs   = nullptr;     // optional (LittleFS/SD)
    const char* sd_path = "/logs/debug.log";
    bool sd_enabled = false;

    // ring buffer for web stream
    size_t ring_lines = 160;       // last N lines kept
    size_t max_line   = 256;       // max bytes per line stored
  };

  // init once at boot
  void begin(Stream* primary = &Serial);

  // sinks
  void setMirror(Stream* mirror);                 // e.g. TCP console stream
  void setSd(fs::FS* fs, const char* path);       // provide filesystem + file path
  void enableSd(bool on);

  // per-module levels
  void setModuleLevel(const char* module, Level lvl);
  Level getModuleLevel(const char* module);

  // log helpers
  bool wouldLog(const char* module, Level lvl);

  void vprintf(const char* module, Level lvl, const char* fmt, va_list ap);
  void printf(const char* module, Level lvl, const char* fmt, ...);
  void println(const char* module, Level lvl, const char* msg);
  void println(const char* module, Level lvl, const String& msg);

  // WebUI stream
  // - lines() returns a single blob (joined with \n) of the last ring buffer lines
  // - clearLines clears ring buffer
  String lines();
  void clearLines();

  // Global config access (rare)
  Config& cfg();
} // namespace Debug

// ============================================================
// Module registry (small fixed list, no dynamic alloc required)
// ============================================================
namespace DebugRegistry {
  // returns module index or -1 on overflow
  int  registerModule(const char* name);
  bool isKnown(const char* name);
  void dump(Stream& out);
}


// ============================================================
// Convenience macros
// - Use D_<MODULE>LN("text") for info-level line logs
// - Use DBG_<MODULE>(lvl, "fmt", ...) for formatted logs
// ============================================================

#define DBG_REGISTER_MODULE(NAME_LIT) \
  static int _dbg_mod_idx_##__LINE__ = DebugRegistry::registerModule(NAME_LIT)

// formatted
#define DBG_LOG(MOD, LVL, FMT, ...) do { Debug::printf((MOD), (LVL), (FMT), ##__VA_ARGS__); } while(0)
#define DBG_LOGLN(MOD, LVL, MSG)    do { Debug::println((MOD), (LVL), (MSG)); } while(0)

// ----- common modules (Info line) -----
#define D_MAINLN(MSG)    DBG_LOGLN("MAIN",    Debug::Level::Info, (MSG))
#define D_WIFILN(MSG)    DBG_LOGLN("WIFI",    Debug::Level::Info, (MSG))
#define D_TCPLN(MSG)     DBG_LOGLN("TCP",     Debug::Level::Info, (MSG))
#define D_UARTLN(MSG)    DBG_LOGLN("UART",    Debug::Level::Info, (MSG))
#define D_WEBLN(MSG)     DBG_LOGLN("WEB",     Debug::Level::Info, (MSG))
#define D_STORAGELN(MSG) DBG_LOGLN("STORAGE", Debug::Level::Info, (MSG))
#define D_BACKUPLN(MSG)  DBG_LOGLN("BACKUP",  Debug::Level::Info, (MSG))
#define D_BP_RTLN(MSG)   DBG_LOGLN("BP_RT",   Debug::Level::Info, (MSG))

// ----- aliases some code expects -----
#define DBG_MAIN(...)    DBG_LOG("MAIN",    Debug::Level::Debug, __VA_ARGS__)
#define DBG_WIFI(...)    DBG_LOG("WIFI",    Debug::Level::Debug, __VA_ARGS__)
#define DBG_TCP(...)     DBG_LOG("TCP",     Debug::Level::Debug, __VA_ARGS__)
#define DBG_UART(...)    DBG_LOG("UART",    Debug::Level::Debug, __VA_ARGS__)
#define DBG_WEB(...)     DBG_LOG("WEB",     Debug::Level::Debug, __VA_ARGS__)
#define DBG_STORAGE(...) DBG_LOG("STORAGE", Debug::Level::Debug, __VA_ARGS__)
#define DBG_BACKUP(...)  DBG_LOG("BACKUP",  Debug::Level::Debug, __VA_ARGS__)
#define DBG_BP_RT(...)   DBG_LOG("BP_RT",   Debug::Level::Debug, __VA_ARGS__)


// ----- extra modules used in codebase -----
#define D_OTALN(MSG)     DBG_LOGLN("OTA",     Debug::Level::Info, (MSG))
#define D_OTA(...)       DBG_LOG("OTA",     Debug::Level::Debug, __VA_ARGS__)
#define D_STORELN(MSG)   DBG_LOGLN("STORAGE", Debug::Level::Info, (MSG))
#define D_STORE(...)     DBG_LOG("STORAGE", Debug::Level::Debug, __VA_ARGS__)
#define D_RESTORELN(MSG) DBG_LOGLN("RESTORE", Debug::Level::Info, (MSG))
#define D_RESTORE(...)   DBG_LOG("RESTORE", Debug::Level::Debug, __VA_ARGS__)

// Compatibility: legacy DBG_PRINTF("fmt", ...) -> MAIN/Debug
#define DBG_PRINTF(FMT, ...) Debug::printf("MAIN", Debug::Level::Debug, (FMT), ##__VA_ARGS__)

// printf-style shortcuts (some legacy files use these)
#define D_MAIN(...)    DBG_LOG("MAIN",    Debug::Level::Debug, __VA_ARGS__)
#define D_WIFI(...)    DBG_LOG("WIFI",    Debug::Level::Debug, __VA_ARGS__)
#define D_TCP(...)     DBG_LOG("TCP",     Debug::Level::Debug, __VA_ARGS__)
#define D_UART(...)    DBG_LOG("UART",    Debug::Level::Debug, __VA_ARGS__)
#define D_WEB(...)     DBG_LOG("WEB",     Debug::Level::Debug, __VA_ARGS__)
#define D_BACKUP(...)  DBG_LOG("BACKUP",  Debug::Level::Debug, __VA_ARGS__)
#define D_STORAGE(...) DBG_LOG("STORAGE", Debug::Level::Debug, __VA_ARGS__)

