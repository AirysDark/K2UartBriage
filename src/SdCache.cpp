#include "SdCache.h"
#include "Debug.h"
#include "Pins_sd.h"
#include "AppConfig.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

DBG_REGISTER_MODULE(__FILE__);

// Paths on SD

static bool g_mounted = false;

static const char* pathFor(SdItem item) {
  return (item == SdItem::Backup) ? CFG_PATH_FW_FILE : CFG_PATH_FW_DIR;
}

bool SdCache::begin() {
  // Use default SPI object (portable across ESP32-S3 cores)
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  if (SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ)) {
    g_mounted = true;
    return true;
  }

  g_mounted = false;
  return false;
}

bool SdCache::mounted() {
  return g_mounted;
}

bool SdCache::exists(SdItem item) {
  if (!g_mounted) return false;
  return SD.exists(pathFor(item));
}

size_t SdCache::sizeBytes(SdItem item) {
  if (!g_mounted) return 0;
  File f = SD.open(pathFor(item), FILE_READ);
  if (!f) return 0;
  size_t s = (size_t)f.size();
  f.close();
  return s;
}

bool SdCache::remove(SdItem item) {
  if (!g_mounted) return false;
  const char* p = pathFor(item);
  if (!SD.exists(p)) return true;
  return SD.remove(p);
}

bool SdCache::writeFileAtomic(SdItem item, const uint8_t* data, size_t len) {
  if (!g_mounted) return false;
  if (!data || !len) return false;

  const char* finalPath = pathFor(item);
  String tmp = String(finalPath) + ".tmp";

  // Remove temp if it exists
  if (SD.exists(tmp.c_str())) SD.remove(tmp.c_str());

  File f = SD.open(tmp.c_str(), FILE_WRITE);
  if (!f) return false;

  size_t w = f.write(data, len);
  f.flush();
  f.close();

  if (w != len) {
    SD.remove(tmp.c_str());
    return false;
  }

  // Replace old
  if (SD.exists(finalPath)) SD.remove(finalPath);
  if (!SD.rename(tmp.c_str(), finalPath)) {
    SD.remove(tmp.c_str());
    return false;
  }
  return true;
}

File SdCache::openRead(SdItem item) {
  if (!g_mounted) return File();
  return SD.open(pathFor(item), FILE_READ);
}

String SdCache::statusJson() {
  JsonDocument d;
  d["mounted"] = g_mounted;

  bool be = exists(SdItem::Backup);
  bool fe = exists(SdItem::Firmware);

  d["backup_exists"]   = be;
  d["backup_size"]     = be ? (uint64_t)sizeBytes(SdItem::Backup) : 0ULL;
  d["firmware_exists"] = fe;
  d["firmware_size"]   = fe ? (uint64_t)sizeBytes(SdItem::Firmware) : 0ULL;

  String out;
  serializeJson(d, out);
  return out;
}