// ============================================================
// env_parse.cpp (FULL COPY/PASTE)
// Fix: ArduinoJson v7 deprecations
// - Replace StaticJsonDocument<> with JsonDocument
// - Replace createNestedObject() with doc["key"].to<JsonObject>()
// ============================================================
#include "Env_parse.h"
#include "Debug.h"
#include <ArduinoJson.h>

DBG_REGISTER_MODULE(__FILE__);

namespace EnvParse {

static inline bool isLineBreak(char c){ return c=='\n' || c=='\r'; }

String sanitizeId(const String& s){
  String out; out.reserve(s.length());
  for (size_t i=0;i<s.length();i++){
    char c = s[i];
    bool ok = (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';
    if (ok) out += c;
  }
  if (out.length() == 0) out = "unknown";
  if (out.length() > 64) out.remove(64);
  return out;
}

String get(const String& env, const char* key){
  if (!key || !*key) return "";
  const String k = String(key) + "=";
  int pos = env.indexOf(k);
  if (pos < 0) return "";
  int start = pos + k.length();
  int end = start;
  while (end < (int)env.length() && !isLineBreak(env[end])) end++;
  String v = env.substring(start, end);
  v.trim();
  return v;
}

static String tinyHash8(const String& s){
  // Simple deterministic 32-bit FNV-1a, render 8 hex chars
  uint32_t h = 2166136261u;
  for (size_t i=0;i<s.length();i++){
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)h);
  return String(buf);
}

String inferBoardId(const String& env){
  String v;

  // Try strong identifiers first
  v = get(env, "chipid");     if (v.length()) return "chipid_"  + sanitizeId(v);
  v = get(env, "serial#");    if (v.length()) return "serial_"  + sanitizeId(v);
  v = get(env, "serial");     if (v.length()) return "serial_"  + sanitizeId(v);
  v = get(env, "soc");        if (v.length()) return "soc_"     + sanitizeId(v);
  v = get(env, "board");      if (v.length()) return "board_"   + sanitizeId(v);
  v = get(env, "board_name"); if (v.length()) return "board_"   + sanitizeId(v);
  v = get(env, "model");      if (v.length()) return "model_"   + sanitizeId(v);
  v = get(env, "product");    if (v.length()) return "product_" + sanitizeId(v);

  // Common unique-ish values
  v = get(env, "ethaddr");    if (v.length()) return "eth_"     + sanitizeId(v);
  v = get(env, "mac");        if (v.length()) return "mac_"     + sanitizeId(v);

  // Fall back: hash of the env (bounded)
  String sample = env;
  if (sample.length() > 2048) sample.remove(2048);
  return "unknown_" + tinyHash8(sample);
}

String layoutHintJson(const String& env){
  // ArduinoJson v7: JsonDocument replaces StaticJsonDocument
  JsonDocument d;

  const char* keys[] = {
    "soc","chipid","serial#","board","board_name","model","product",
    "bootcmd","bootargs","console","partitions","mtdparts","root","rootfstype",
    "mmcdev","mmcpart","boot_targets"
  };

  // v7: createNestedObject() deprecated -> use doc["env"].to<JsonObject>()
  JsonObject o = d["env"].to<JsonObject>();

  for (auto k : keys){
    String v = get(env, k);
    if (v.length()){
      // cap to keep JSON small
      if (v.length() > 200) { v.remove(200); v += "..."; }
      o[k] = v;
    }
  }

  // quick flags
  d["has_partitions"] = get(env, "partitions").length() > 0 || env.indexOf("partitions=") >= 0;
  d["has_mtdparts"]   = get(env, "mtdparts").length() > 0   || env.indexOf("mtdparts=") >= 0;
  d["has_bootargs"]   = get(env, "bootargs").length() > 0   || env.indexOf("bootargs=") >= 0;

  String out;
  serializeJson(d, out);
  return out;
}

} // namespace EnvParse