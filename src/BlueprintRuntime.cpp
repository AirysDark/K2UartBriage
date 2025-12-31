#include "BlueprintRuntime.h"
#include "AppConfig.h"
#include "Debug.h"

#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

DBG_REGISTER_MODULE(__FILE__);

// IMPORTANT: include the library, but do NOT assume methods that don't exist.
#include <DeviceBlueprintLib.h>

// NOTE: we deliberately keep this module self-contained.
// Other files should include ONLY BlueprintRuntime.h.

static DeviceBlueprintLib g_bp;      // keep for future expansion
static Stream* g_target = nullptr;
static Stream* g_debug  = nullptr;

static bool   g_inited    = false;

// scripts/prompts/gcode load flags
static bool   g_scriptsOk = false;
static bool   g_promptsOk = false;
static bool   g_gcodeOk   = false;

static BlueprintRuntime::Mode g_mode = BlueprintRuntime::Mode::Unknown;
static String g_lastLine;

// minimal extracted keys
static String g_boardId;
static String g_layoutJson;

// docs cache (ArduinoJson v7: JsonDocument)
static JsonDocument g_scriptsDoc;
static JsonDocument g_promptsDoc;
static JsonDocument g_gcodeDoc;

// ---- Ensure these exist in AppConfig.h (add if missing) ----
//
// #ifndef CFG_BP_ENABLE
//   #define CFG_BP_ENABLE 1
// #endif
// static const char* CFG_BP_DIR          = "/bp";
// static const char* CFG_BP_SCRIPTS_PATH = "/bp/scripts.json";
// static const char* CFG_BP_PROMPTS_PATH = "/bp/prompts.json";
// static const char* CFG_BP_GCODE_PATH   = "/bp/gcode.json";
// static const uint32_t CFG_BP_SCRIPT_STEP_DELAY_MS = 80;
// static const size_t CFG_BP_MAX_LINE = 256;

static void dbg(const String& s) {
  if (g_debug) g_debug->println(s);
}

static void setMode(BlueprintRuntime::Mode m) { g_mode = m; }

// ------------------------------------------------------------
// Try-call helper for unknown library APIs (compile-safe)
// ------------------------------------------------------------
template <typename T>
static auto tryBegin0(T& obj, int) -> decltype(obj.begin(), void()) {
  obj.begin();
}
template <typename T>
static void tryBegin0(T& obj, long) { (void)obj; }

template <typename T>
static auto tryBegin2(T& obj, Stream& a, Stream* b, int) -> decltype(obj.begin(a, b), void()) {
  obj.begin(a, b);
}
template <typename T>
static void tryBegin2(T& obj, Stream& a, Stream* b, long) {
  (void)obj; (void)a; (void)b;
}

static void detectModeFromLine(const String& line) {
  // U-Boot prompt
  if (line.indexOf("=>") >= 0) { setMode(BlueprintRuntime::Mode::UBoot); return; }

  // Linux login prompts
  if (line.indexOf("login:") >= 0 || line.indexOf("k2 login:") >= 0) {
    setMode(BlueprintRuntime::Mode::LinuxLoginUser);
    return;
  }
  if (line.indexOf("Password:") >= 0) {
    setMode(BlueprintRuntime::Mode::LinuxLoginPass);
    return;
  }

  // Linux shell prompt (crude but works)
  if (line.endsWith("#")) { setMode(BlueprintRuntime::Mode::LinuxShell); return; }
  if (line.indexOf("root@") >= 0 && line.indexOf(":") >= 0 && line.endsWith("#")) {
    setMode(BlueprintRuntime::Mode::LinuxShell);
    return;
  }
}

static void extractKeysFromLine(const String& line) {
  // board_id=XXXX
  int b = line.indexOf("board_id=");
  if (b >= 0) {
    String v = line.substring(b + 9);
    v.trim();
    if (v.length()) g_boardId = v;
  }

  // layout_json={...}
  int l = line.indexOf("layout_json=");
  if (l >= 0) {
    String v = line.substring(l + 12);
    v.trim();
    if (v.length()) g_layoutJson = v;
  }
}

// Load a JSON file into a JsonDocument (v7 style)
static bool loadJsonDoc(const char* path, JsonDocument& out, const char* tag) {
  out.clear();

  if (!LittleFS.exists(path)) {
    if (g_debug) g_debug->printf("[BP] %s missing: %s\n", tag, path);
    return false;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    if (g_debug) g_debug->printf("[BP] open %s failed\n", tag);
    return false;
  }

  size_t sz = f.size();
  if (sz > 256 * 1024) {
    if (g_debug) g_debug->printf("[BP] %s too large\n", tag);
    f.close();
    return false;
  }

  DeserializationError err = deserializeJson(out, f);
  f.close();

  if (err) {
    if (g_debug) g_debug->printf("[BP] %s parse error: %s\n", tag, err.c_str());
    out.clear();
    return false;
  }

  return true;
}

// --------------------- scripts.json helpers ---------------------
static bool scriptLookup(const String& name, JsonVariant& outSteps) {
  JsonVariant root = g_scriptsDoc.as<JsonVariant>();
  if (root.isNull()) return false;

  // Supported shapes:
  // A) { "scripts": { "boot_normal": ["cmd","cmd"] } }
  // B) { "boot_normal": ["cmd","cmd"] }
  // C) { "scripts": [ { "name":"boot_normal", "steps":[...]} ] }

  if (root.is<JsonObject>()) {
    JsonObject obj = root.as<JsonObject>();

    JsonVariant s = obj["scripts"];
    if (!s.isNull()) {
      if (s.is<JsonObject>()) {
        JsonObject so = s.as<JsonObject>();
        JsonVariant v = so[name];
        if (!v.isNull()) { outSteps = v; return true; }
      } else if (s.is<JsonArray>()) {
        for (JsonVariant it : s.as<JsonArray>()) {
          if (!it.is<JsonObject>()) continue;
          JsonObject o = it.as<JsonObject>();
          const char* n = o["name"] | "";
          if (String(n) == name) { outSteps = o["steps"]; return true; }
        }
      }
    }

    JsonVariant v2 = obj[name];
    if (!v2.isNull()) { outSteps = v2; return true; }
  }

  return false;
}

static String listScriptsCsvInternal() {
  JsonVariant root = g_scriptsDoc.as<JsonVariant>();
  if (root.isNull()) return "";

  String out;

  if (root.is<JsonObject>()) {
    JsonObject obj = root.as<JsonObject>();

    JsonVariant s = obj["scripts"];
    if (!s.isNull()) {
      if (s.is<JsonObject>()) {
        for (JsonPair kv : s.as<JsonObject>()) {
          if (out.length()) out += ",";
          out += kv.key().c_str();
        }
        return out;
      }
      if (s.is<JsonArray>()) {
        for (JsonVariant it : s.as<JsonArray>()) {
          const char* n = it["name"] | "";
          if (!n[0]) continue;
          if (out.length()) out += ",";
          out += n;
        }
        return out;
      }
    }

    for (JsonPair kv : obj) {
      if (String(kv.key().c_str()) == "meta") continue;
      if (out.length()) out += ",";
      out += kv.key().c_str();
    }
  }

  return out;
}

// --------------------- prompts.json helpers ---------------------
static JsonVariant promptsRoot() {
  JsonVariant root = g_promptsDoc.as<JsonVariant>();
  if (root.isNull()) return JsonVariant();

  if (root.is<JsonObject>()) {
    JsonVariant p = root["prompts"];
    if (!p.isNull()) return p;
  }
  return root;
}

static String listPromptsCsvInternal() {
  JsonVariant pr = promptsRoot();
  if (pr.isNull() || !pr.is<JsonObject>()) return "";

  String out;
  for (JsonPair kv : pr.as<JsonObject>()) {
    if (out.length()) out += ",";
    out += kv.key().c_str();
  }
  return out;
}

static String promptTextInternal(const String& name) {
  JsonVariant pr = promptsRoot();
  if (pr.isNull()) return "";

  JsonVariant v = pr[name];
  if (v.isNull()) return "";

  // string
  if (v.is<const char*>()) {
    return String(v.as<const char*>());
  }

  // array of lines
  if (v.is<JsonArray>()) {
    String out;
    for (JsonVariant it : v.as<JsonArray>()) {
      if (it.isNull()) continue;
      if (it.is<const char*>()) {
        String line = String(it.as<const char*>());
        if (!line.length()) continue;
        if (out.length()) out += "\n";
        out += line;
      }
    }
    return out;
  }

  return "";
}

// --------------------- gcode.json helpers ---------------------
static JsonVariant gcodeGroupsRoot() {
  JsonVariant root = g_gcodeDoc.as<JsonVariant>();
  if (root.isNull() || !root.is<JsonObject>()) return JsonVariant();

  JsonVariant g = root["groups"];
  if (!g.isNull()) return g;

  return JsonVariant();
}

static String listGcodeGroupsCsvInternal() {
  JsonVariant gr = gcodeGroupsRoot();
  if (gr.isNull() || !gr.is<JsonObject>()) return "";

  String out;
  for (JsonPair kv : gr.as<JsonObject>()) {
    if (out.length()) out += ",";
    out += kv.key().c_str();
  }
  return out;
}

static String listGcodeNamesCsvInternal(const String& group) {
  JsonVariant gr = gcodeGroupsRoot();
  if (gr.isNull() || !gr.is<JsonObject>()) return "";

  JsonVariant g = gr[group];
  if (g.isNull() || !g.is<JsonObject>()) return "";

  String out;
  for (JsonPair kv : g.as<JsonObject>()) {
    if (out.length()) out += ",";
    out += kv.key().c_str();
  }
  return out;
}

static String getGcodeLineInternal(const String& group, const String& name) {
  JsonVariant gr = gcodeGroupsRoot();
  if (gr.isNull() || !gr.is<JsonObject>()) return "";

  JsonVariant g = gr[group];
  if (g.isNull() || !g.is<JsonObject>()) return "";

  JsonVariant v = g[name];
  if (v.isNull()) return "";

  if (v.is<const char*>()) return String(v.as<const char*>());
  return "";
}

// ------------------------------------------------------------

bool BlueprintRuntime::begin(Stream& target, Stream* debug) {
#if !CFG_BP_ENABLE
  (void)target; (void)debug;
  return false;
#endif
  if (g_inited) return true;

  g_target = &target;
  g_debug  = debug;

  if (!LittleFS.begin(true)) {
    dbg("[BP] LittleFS mount failed (assets disabled)");
  } else {
    if (!LittleFS.exists(CFG_BP_DIR)) LittleFS.mkdir(CFG_BP_DIR);
  }

  // Attempt common begin() shapes without assuming library API.
  // 1) begin()                  2) begin(target, debug)
  tryBegin0(g_bp, 0);
  tryBegin2(g_bp, target, debug, 0);

  // Load assets (optional)
  g_scriptsOk = loadJsonDoc(CFG_BP_SCRIPTS_JSON, g_scriptsDoc, "scripts.json");
  g_promptsOk = loadJsonDoc(CFG_BP_PROMPTS_JSON, g_promptsDoc, "prompts.json");
  g_gcodeOk   = loadJsonDoc(CFG_BP_GCODE_JSON,   g_gcodeDoc,   "gcode.json");

  if (g_debug) {
    g_debug->printf("[BP] init ok. scripts=%s prompts=%s gcode=%s\n",
      g_scriptsOk ? "OK" : "missing",
      g_promptsOk ? "OK" : "missing",
      g_gcodeOk   ? "OK" : "missing"
    );
  }

  g_inited = true;
  return true;
}

void BlueprintRuntime::feedBytes(const uint8_t* data, size_t len) {
#if !CFG_BP_ENABLE
  (void)data; (void)len;
  return;
#endif
  if (!g_inited || !data || !len) return;

  // Feed bytes to the library using the ONLY confirmed API: feedTargetChar
  for (size_t i = 0; i < len; i++) {
    g_bp.feedTargetChar((char)data[i]);
  }

  // Also build a rolling lastLine for state detection
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\r') c = '\n';

    if (c == '\n') {
      BlueprintRuntime::feedLine(g_lastLine);
      g_lastLine = "";
    } else if ((uint8_t)c >= 0x20 && (uint8_t)c <= 0x7E) {
      g_lastLine += c;
      if (g_lastLine.length() > (size_t)CFG_BP_MAX_LINE) {
        g_lastLine.remove(0, g_lastLine.length() - (size_t)CFG_BP_MAX_LINE);
      }
    }
  }
}

void BlueprintRuntime::feedLine(const String& raw) {
#if !CFG_BP_ENABLE
  (void)raw;
  return;
#endif
  if (!g_inited) return;

  String line = raw;
  line.trim();
  if (!line.length()) return;

  // track last line
  g_lastLine = line;

  detectModeFromLine(line);
  extractKeysFromLine(line);
}

void BlueprintRuntime::tick() {
  // Reserved (future: timeouts, script progress, etc.)
}

BlueprintRuntime::Mode BlueprintRuntime::mode() {
  if (!g_inited) return BlueprintRuntime::Mode::Unknown;
  return g_mode;
}

const String& BlueprintRuntime::lastLine() {
  static String empty;
  if (!g_inited) return empty;
  return g_lastLine;
}

String BlueprintRuntime::getKey(const String& k) {
  if (!g_inited) return "";
  if (k.equalsIgnoreCase("board_id")) return g_boardId;
  if (k.equalsIgnoreCase("layout_json")) return g_layoutJson;
  return "";
}

String BlueprintRuntime::listKeysCsv() {
  return "board_id,layout_json";
}

String BlueprintRuntime::listScriptsCsv() {
  if (!g_inited) return "";
  if (!g_scriptsOk) return "";
  return listScriptsCsvInternal();
}

bool BlueprintRuntime::runScript(const String& name, uint32_t timeoutMs) {
  (void)timeoutMs; // best-effort runner; future: add prompt-based timeout

  if (!g_inited || !g_target) return false;
  if (!g_scriptsOk) return false;

  JsonVariant steps;
  if (!scriptLookup(name, steps)) return false;

  auto sendLine = [&](const String& cmd) {
    if (!cmd.length()) return;
    g_target->print(cmd);
    g_target->print("\n");
    delay((uint32_t)CFG_BP_SCRIPT_STEP_DELAY_MS);
  };

  if (steps.is<const char*>()) {
    String cmd = String(steps.as<const char*>());
    cmd.trim();
    if (cmd.length() && !cmd.startsWith("#")) sendLine(cmd);
    return true;
  }

  if (!steps.is<JsonArray>()) return false;

  for (JsonVariant v : steps.as<JsonArray>()) {
    if (v.isNull()) continue;

    if (v.is<const char*>()) {
      String cmd = String(v.as<const char*>());
      cmd.trim();
      if (!cmd.length() || cmd.startsWith("#")) continue;
      sendLine(cmd);
      continue;
    }

    if (v.is<JsonObject>()) {
      JsonObject o = v.as<JsonObject>();
      String cmd = String((const char*)(o["cmd"] | ""));
      cmd.trim();
      if (!cmd.length() || cmd.startsWith("#")) continue;

      uint32_t d = (uint32_t)(o["delay"] | (uint32_t)CFG_BP_SCRIPT_STEP_DELAY_MS);
      g_target->print(cmd);
      g_target->print("\n");
      delay(d);
      continue;
    }
  }

  return true;
}

// ---- Prompts ----
String BlueprintRuntime::listPromptsCsv() {
  if (!g_inited) return "";
  if (!g_promptsOk) return "";
  return listPromptsCsvInternal();
}

String BlueprintRuntime::getPromptText(const String& name) {
  if (!g_inited) return "";
  if (!g_promptsOk) return "";
  return promptTextInternal(name);
}

// ---- Gcode/Preset ----
String BlueprintRuntime::listGcodeGroupsCsv() {
  if (!g_inited) return "";
  if (!g_gcodeOk) return "";
  return listGcodeGroupsCsvInternal();
}

String BlueprintRuntime::listGcodeNamesCsv(const String& group) {
  if (!g_inited) return "";
  if (!g_gcodeOk) return "";
  return listGcodeNamesCsvInternal(group);
}

String BlueprintRuntime::getGcodeLine(const String& group, const String& name) {
  if (!g_inited) return "";
  if (!g_gcodeOk) return "";
  return getGcodeLineInternal(group, name);
}

bool BlueprintRuntime::sendGcode(const String& group, const String& name) {
  if (!g_inited || !g_target) return false;
  if (!g_gcodeOk) return false;

  String line = getGcodeLineInternal(group, name);
  line.trim();
  if (!line.length()) return false;

  g_target->print(line);
  g_target->print("\n");
  return true;
}

// ---- Asset flags ----
bool BlueprintRuntime::assetsLoaded()   { return g_scriptsOk; }
bool BlueprintRuntime::promptsLoaded()  { return g_promptsOk; }
bool BlueprintRuntime::gcodeLoaded()    { return g_gcodeOk; }