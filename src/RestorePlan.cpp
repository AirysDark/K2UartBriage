#include "RestorePlan.h"
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static String readAllFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s;
  while (f.available()) s += char(f.read());
  f.close();
  return s;
}

bool RestorePlan::begin() {
  // Safe mount: won't break if already mounted
  if (!LittleFS.begin(true)) {
    return false;
  }
  // Ensure directory exists (optional)
  if (!LittleFS.exists("/restore")) LittleFS.mkdir("/restore");
  return true;
}

bool RestorePlan::loadFromFile(const char* path) {
  _loaded = false;
  _armed = false;
  _imageCount = 0;
  _bootenvCount = 0;

  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(path)) return false;

  String json = readAllFile(path);
  if (!json.length()) return false;

  return parseJson(json);
}

bool RestorePlan::parseJson(const String& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  _format  = String((const char*)(doc["format"] | ""));
  _version = (int)(doc["version"] | 0);

  JsonObject t = doc["target"].as<JsonObject>();
  _target.soc = String((const char*)(t["soc"] | ""));
  _target.scheme = String((const char*)(t["scheme"] | ""));
  _target.by_name_base = String((const char*)(t["by_name_base"] | ""));

  _profile = String((const char*)(doc["profile"] | ""));
  _notes   = String((const char*)(doc["notes"] | ""));

  // Images
  JsonArray imgs = doc["images"].as<JsonArray>();
  if (!imgs.isNull()) {
    for (JsonVariant v : imgs) {
      if (_imageCount >= 8) break;
      JsonObject o = v.as<JsonObject>();
      Image& im = _images[_imageCount++];
      im.filename = String((const char*)(o["filename"] | ""));
      im.device   = String((const char*)(o["device"] | ""));
      im.installed_directly = (bool)(o["installed_directly"] | false);
      im.type     = String((const char*)(o["type"] | ""));
    }
  }

  // Bootenv
  JsonArray env = doc["bootenv"].as<JsonArray>();
  if (!env.isNull()) {
    for (JsonVariant v : env) {
      if (_bootenvCount >= 16) break;
      JsonObject o = v.as<JsonObject>();
      BootEnvKV& kv = _bootenv[_bootenvCount++];
      kv.name  = String((const char*)(o["name"] | ""));
      kv.value = String((const char*)(o["value"] | ""));
    }
  }

  // Minimal validation
  if (_format != "k2_restore") return false;
  if (_version != 1) return false;
  if (_imageCount == 0) return false;
  if (!_profile.length()) return false;

  _loaded = true;
  return true;
}

String RestorePlan::arm(const String& token, bool overrideBoardId) {
  if (!_loaded) return "restore arm: FAIL (no plan loaded)";
  // token is optional for now; you can enforce it later
  (void)token;
  (void)overrideBoardId;

  _armed = true;
  _armedAtMs = millis();
  return "restore arm: OK (unsafe window open)";
}

void RestorePlan::disarm() {
  _armed = false;
  _armedAtMs = 0;
}

String RestorePlan::planText() const {
  if (!_loaded) return "(no restore plan loaded)";

  String s;
  s += "format=" + _format + " version=" + String(_version) + "\n";
  s += "target.soc=" + _target.soc + " scheme=" + _target.scheme + "\n";
  s += "profile=" + _profile + "\n";
  if (_notes.length()) s += "notes=" + _notes + "\n";

  s += "images:\n";
  for (size_t i = 0; i < _imageCount; i++) {
    const Image& im = _images[i];
    s += "  - type=" + im.type;
    if (im.filename.length()) s += " file=" + im.filename;
    if (im.device.length())   s += " dev=" + im.device;
    s += " directly=" + String(im.installed_directly ? "true" : "false");
    s += "\n";
  }

  s += "bootenv:\n";
  for (size_t i = 0; i < _bootenvCount; i++) {
    s += "  " + _bootenv[i].name + "=" + _bootenv[i].value + "\n";
  }
  return s;
}

String RestorePlan::verifyText() const {
  if (!_loaded) return "restore verify: FAIL (no plan loaded)";

  String out = "restore verify:\n";

  // Check that referenced payload files exist (only if stored on LittleFS)
  // If you store payload on SD, you can extend this to SD.exists().
  for (size_t i = 0; i < _imageCount; i++) {
    const Image& im = _images[i];
    if (!im.filename.length()) continue;

    // We accept both "payload/kernel" and "/payload/kernel"
    String p = im.filename;
    if (!p.startsWith("/")) p = "/" + p;

    bool exists = LittleFS.exists(p);
    out += "  file " + p + " : " + (exists ? "OK" : "MISSING") + "\n";
  }

  out += "  NOTE: block writes require Linux side (/dev/by-name). U-Boot-only restore needs LBA map.\n";
  return out;
}

String RestorePlan::applyText() const {
  if (!_loaded) return "restore apply: FAIL (no plan loaded)";

  // timeout auto-disarm
  if (_armed) {
    uint32_t age = millis() - _armedAtMs;
    if (age > ARM_TIMEOUT_MS) {
      return "restore apply: FAIL (armed expired, re-arm required)";
    }
  } else {
    return "restore apply: FAIL (not armed) ? run !restore arm";
  }

  // SAFE OUTPUT: print the commands to run on the *K2 Linux shell*
  String out;
  out += "restore apply (SAFE MODE): run these on K2 Linux shell:\n";
  out += "------------------------------------------------------\n";

  // For block images: dd -> device
  for (size_t i = 0; i < _imageCount; i++) {
    const Image& im = _images[i];
    if (im.type == "block") {
      if (!im.filename.length() || !im.device.length()) {
        out += "# SKIP invalid block entry\n";
        continue;
      }
      out += "dd if=" + im.filename + " of=" + im.device + " bs=4M conv=fsync\n";
    } else if (im.type == "awuboot") {
      out += "# uboot is type=awuboot ? needs vendor tool or known write method\n";
      out += "# file: " + im.filename + "\n";
    }
  }

  // bootenv
  out += "\n# bootenv (U-Boot env / fw_setenv equivalent):\n";
  for (size_t i = 0; i < _bootenvCount; i++) {
    out += "fw_setenv " + _bootenv[i].name + " \"" + _bootenv[i].value + "\"\n";
  }

  out += "\nreboot\n";
  return out;
}