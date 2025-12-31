#pragma once
#include "Debug.h"

#include <Arduino.h>
#include <FS.h>

// ============================================================
// SdCache
// - Keeps ONE cached backup and ONE cached firmware on microSD.
// - New saves overwrite old.
// - Web UI can query status, download, delete.
// ============================================================

enum class SdItem : uint8_t { Backup = 0, Firmware = 1 };

namespace SdCache {

// Call once at boot.
// Returns true if SD mounted.
bool begin();

bool mounted();

// True if cached file exists.
bool exists(SdItem item);

// Returns file size, or 0.
size_t sizeBytes(SdItem item);

// Remove cached file.
bool remove(SdItem item);

// Save cache (atomic temp + rename; overwrites existing).
bool writeFileAtomic(SdItem item, const uint8_t* data, size_t len);

// Convenience wrappers
inline bool saveBackup(const uint8_t* data, size_t len)   { return writeFileAtomic(SdItem::Backup, data, len); }
inline bool saveFirmware(const uint8_t* data, size_t len) { return writeFileAtomic(SdItem::Firmware, data, len); }

// Open for download streaming
// Caller must close.
File openRead(SdItem item);

// JSON: {mounted, backup_exists, backup_size, firmware_exists, firmware_size}
String statusJson();

} // namespace SdCache
