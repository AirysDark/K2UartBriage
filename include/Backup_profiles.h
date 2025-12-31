#pragma once
#include <Arduino.h>
#include "AppConfig.h"
#include "Debug.h"
#include <ArduinoJson.h>

// ============================================================
// Backup profiles (SELECTABLE A/B/C/FULL + Custom)
// ============================================================

struct BackupProfileRange {
  uint32_t lba_start;
  uint32_t lba_count;

  constexpr BackupProfileRange(uint32_t start = 0, uint32_t count = 0)
    : lba_start(start), lba_count(count) {}
};

struct BackupProfile {
  const char* id;      // "A","B","C","FULL","CUSTOM"
  const char* name;
  const char* desc;
  BackupProfileRange range;
};

// NOTE:
// Use explicit constructor calls for BackupProfileRange.
// This avoids Arduino / GCC / IntelliSense brace-init bugs.

static const BackupProfile PROFILES[] = {
  {
    "A",
    "Option A (Bootchain only)",
    "SPL+U-Boot region (small, safest)",
    BackupProfileRange(CFG_BACKUP_PROFILE_A_LBA_START, CFG_BACKUP_PROFILE_A_LBA_COUNT)    // 8MB
  },
  {
    "B",
    "Option B (Boot + kernel)",
    "Adds kernel/dtb region (medium)",
    BackupProfileRange(CFG_BACKUP_PROFILE_B_LBA_START, CFG_BACKUP_PROFILE_B_LBA_COUNT)    // 64MB
  },
  {
    "C",
    "Option C (Early partitions)",
    "First chunk of storage (large)",
    BackupProfileRange(CFG_BACKUP_PROFILE_C_LBA_START, CFG_BACKUP_PROFILE_C_LBA_COUNT)    // 256MB
  },
  {
    "FULL",
    "Full (All)",
    "Attempt full device (VERY slow UART)",
    BackupProfileRange(CFG_BACKUP_PROFILE_FULL_LBA_START, CFG_BACKUP_PROFILE_FULL_LBA_COUNT)   // 2GB placeholder
  },
  {
    "CUSTOM",
    "Custom",
    "User-defined LBA range",
    BackupProfileRange(0UL, 0UL)
  }
};

static inline const BackupProfile* findProfile(const String& id) {
  for (const auto& p : PROFILES) {
    if (id.equalsIgnoreCase(p.id)) return &p;
  }
  return &PROFILES[0];
}

static inline void profileToJson(JsonObject o, const BackupProfile& p) {
  o["id"] = p.id;
  o["name"] = p.name;
  o["desc"] = p.desc;
  o["lba_start"] = p.range.lba_start;
  o["lba_count"] = p.range.lba_count;
}
