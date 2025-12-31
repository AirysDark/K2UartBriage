#include <Arduino.h>
#include <FS.h>
#include "Debug.h"

// NOTE: keep this file Arduino-friendly (no heavy STL).
// We use a tiny fixed module table + ring buffer of strings.

namespace {
  Debug::Config g_cfg;

  struct ModEntry {
    const char* name;
    Debug::Level level;
  };

  // small module table (extend as needed)
  constexpr size_t MOD_MAX = 32;
  ModEntry g_mods[MOD_MAX];
  size_t g_mod_count = 0;

  // ring buffer
  struct Ring {
    String* lines = nullptr;
    size_t cap = 0;
    size_t head = 0;
    size_t count = 0;
    bool ready = false;

    void init(size_t capLines){
      if (ready && cap == capLines) return;
      if (lines) { delete[] lines; lines = nullptr; }
      cap = (capLines < 8) ? 8 : capLines;
      lines = new String[cap];
      head = 0;
      count = 0;
      ready = true;
    }

    void push(const String& s){
      if (!ready) init(g_cfg.ring_lines);
      size_t idx = (head + count) % cap;
      if (count < cap){
        lines[idx] = s;
        count++;
      } else {
        // overwrite oldest
        lines[head] = s;
        head = (head + 1) % cap;
      }
    }

    String dump() const {
      if (!ready || count == 0) return String();
      String out;
      // reserve approximate (avoid massive)
      for (size_t i=0;i<count;i++){
        size_t idx = (head + i) % cap;
        out += lines[idx];
        out += '\n';
      }
      return out;
    }

    void clear(){
      head = 0; count = 0;
      if (ready && lines){
        for (size_t i=0;i<cap;i++) lines[i].remove(0);
      }
    }
  };

  Ring g_ring;

  int modIndex(const char* name){
    if (!name) return -1;
    for (size_t i=0;i<g_mod_count;i++){
      if (g_mods[i].name && strcmp(g_mods[i].name, name) == 0) return (int)i;
    }
    return -1;
  }

  void writeTo(Stream* s, const char* buf){
    if (!s || !buf) return;
    s->print(buf);
  }

  void writeSd(const char* buf){
    if (!g_cfg.sd_enabled || !g_cfg.sd_fs || !buf) return;
    File f = g_cfg.sd_fs->open(g_cfg.sd_path, FILE_APPEND);
    if (!f) return;
    f.print(buf);
    f.close();
  }

  const char* lvlTag(Debug::Level lvl){
    switch(lvl){
      case Debug::Level::Error: return "E";
      case Debug::Level::Warn:  return "W";
      case Debug::Level::Info:  return "I";
      case Debug::Level::Debug: return "D";
      case Debug::Level::Trace: return "T";
      default: return "?";
    }
  }
}

namespace Debug {

  Config& cfg(){ return g_cfg; }

  void begin(Stream* primary){
    g_cfg.primary = primary;
    g_ring.init(g_cfg.ring_lines);

    // register some default modules so WebUI can set them
    DebugRegistry::registerModule("MAIN");
    DebugRegistry::registerModule("WIFI");
    DebugRegistry::registerModule("TCP");
    DebugRegistry::registerModule("UART");
    DebugRegistry::registerModule("WEB");
    DebugRegistry::registerModule("STORAGE");
    DebugRegistry::registerModule("BACKUP");
    DebugRegistry::registerModule("BP_RT");
    DebugRegistry::registerModule("OTA");
    DebugRegistry::registerModule("RESTORE");


    println("MAIN", Level::Info, "[Debug] begin()");
  }

  void setMirror(Stream* mirror){ g_cfg.mirror = mirror; }
  void setSd(fs::FS* fs, const char* path){
    g_cfg.sd_fs = fs;
    if (path && *path) g_cfg.sd_path = path;
  }
  void enableSd(bool on){ g_cfg.sd_enabled = on; }

  void setModuleLevel(const char* module, Level lvl){
    int idx = modIndex(module);
    if (idx < 0){
      idx = DebugRegistry::registerModule(module);
    }
    if (idx >= 0) g_mods[idx].level = lvl;
  }

  Level getModuleLevel(const char* module){
    int idx = modIndex(module);
    if (idx < 0) return Level::Info;
    return g_mods[idx].level;
  }

  bool wouldLog(const char* module, Level lvl){
    if (!g_cfg.enabled) return false;
    // default: allow up to Info if module not known
    Level m = getModuleLevel(module);
    return (uint8_t)lvl <= (uint8_t)m;
  }

  void vprintf(const char* module, Level lvl, const char* fmt, va_list ap){
    if (!wouldLog(module, lvl)) return;

    char line[320];
    // prefix
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "[%s][%s] ", module ? module : "?", lvlTag(lvl));

    int n = vsnprintf(line, sizeof(line), fmt ? fmt : "", ap);
    (void)n;

    // ensure newline
    String full = String(prefix) + String(line);
    if (!full.endsWith("\n")) full += "\n";

    // sinks
    writeTo(g_cfg.primary, full.c_str());
    writeTo(g_cfg.mirror,  full.c_str());
    writeSd(full.c_str());

    // ring buffer (trim)
    if (full.length() > g_cfg.max_line){
      full.remove(g_cfg.max_line);
      if (!full.endsWith("\n")) full += "\n";
    }
    g_ring.push(full);
  }

  void printf(const char* module, Level lvl, const char* fmt, ...){
    va_list ap; va_start(ap, fmt);
    vprintf(module, lvl, fmt, ap);
    va_end(ap);
  }

  void println(const char* module, Level lvl, const char* msg){
    if (!wouldLog(module, lvl)) return;
    const char* m = msg ? msg : "";
    printf(module, lvl, "%s", m);
  }

  void println(const char* module, Level lvl, const String& msg){
    println(module, lvl, msg.c_str());
  }

  String lines(){ return g_ring.dump(); }
  void clearLines(){ g_ring.clear(); }

} // namespace Debug

namespace DebugRegistry {

  int registerModule(const char* name){
    if (!name || !*name) return -1;
    int idx = modIndex(name);
    if (idx >= 0) return idx;
    if (g_mod_count >= MOD_MAX) return -1;
    g_mods[g_mod_count].name = name;
    g_mods[g_mod_count].level = Debug::Level::Info; // default calm
    return (int)g_mod_count++;
  }

  bool isKnown(const char* name){
    return modIndex(name) >= 0;
  }
  void dump(Stream& out){
    out.println(F("---- DebugRegistry ----"));
    out.printf("modules=%u (max=%u)\n", (unsigned)g_mod_count, (unsigned)MOD_MAX);
    for (size_t i=0;i<g_mod_count;i++){
      const char* n = g_mods[i].name ? g_mods[i].name : "?";
      out.printf("  [%02u] %-10s lvl=%u\n", (unsigned)i, n, (unsigned)g_mods[i].level);
    }
    out.println(F("-----------------------"));
  }

}
