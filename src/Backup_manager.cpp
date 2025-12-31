#include "Backup_manager.h"
#include <Arduino.h>
#include "Debug.h"
#include <cstdarg>
#include <cstdio>

DBG_REGISTER_MODULE(__FILE__);

// -----------------------------------------------------------------------------
// Backup debug logger (no local config macros; controlled by DEBUG_BACKUP)
// Works on HWCDC (USB CDC) where Serial.vprintf() is not available.
// -----------------------------------------------------------------------------
static inline void backup_logf(const char* fmt, ...) {
#if DEBUG_BACKUP
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  // Normalize legacy log strings that already include "[BACKUP]" and/or trailing newlines.
  String s(buf);
  if (s.startsWith("[BACKUP] ")) s.remove(0, 9);
  s.trim();
  if (s.length()) D_BACKUP(s);
#else
  (void)fmt;
#endif
}

// -----------------------------------------------------------------------------
// Range planning (authoritative via backup_profiles.h findProfile())
// -----------------------------------------------------------------------------
struct ProfileRange {
  uint32_t start;
  uint32_t count;
};

static inline void addRange(std::vector<ProfileRange>& v, uint32_t start, uint32_t count) {
  if (count == 0) return;
  v.push_back(ProfileRange{start, count});
}

static bool appendProfileRanges(const String& profileId,
                                std::vector<ProfileRange>& out,
                                String* err) {
  out.clear();

  if (profileId.equalsIgnoreCase("CUSTOM")) {
    return true;
  }

  const BackupProfile* p = findProfile(profileId);
  if (!p) {
    if (err) *err = "Unknown profile";
    return false;
  }
  if (p->range.lba_count == 0) {
    if (err) *err = "Profile range count is 0";
    return false;
  }

  addRange(out, p->range.lba_start, p->range.lba_count);
  return true;
}

// -----------------------------------------------------------------------------
// BackupManager
// -----------------------------------------------------------------------------
void BackupManager::begin(HardwareSerial* target, Preferences* prefs) {
  _t = target;
  _prefs = prefs;
  loadPrefs();
  backup_logf("[BACKUP] begin (profile=%s)\n", _profileId.c_str());
}

void BackupManager::loadPrefs() {
  if (!_prefs) return;

  // Read-only open, then close. Never leave NVS open.
  _prefs->begin(CFG_PREF_NS_BACKUP, true);
  _profileId   = _prefs->getString(CFG_PREF_KEY_PROFILE, "A");
  _customStart = _prefs->getUInt(CFG_PREF_KEY_CSTART, 0);
  _customCount = _prefs->getUInt(CFG_PREF_KEY_CCOUNT, 0);
  _prefs->end();
}

void BackupManager::savePrefs() {
  if (!_prefs) return;

  // RW open, then close.
  _prefs->begin(CFG_PREF_NS_BACKUP, false);
  _prefs->putString(CFG_PREF_KEY_PROFILE, _profileId);
  _prefs->putUInt(CFG_PREF_KEY_CSTART, _customStart);
  _prefs->putUInt(CFG_PREF_KEY_CCOUNT, _customCount);
  _prefs->end();
}

void BackupManager::setProfileId(const String& id) {
  _profileId = id;
  savePrefs();
}

String BackupManager::getProfileId() const {
  return _profileId;
}

void BackupManager::setCustomRange(uint32_t start, uint32_t count) {
  _customStart = start;
  _customCount = count;
  savePrefs();
}

void BackupManager::getCustomRange(uint32_t& start, uint32_t& count) const {
  start = _customStart;
  count = _customCount;
}

bool BackupManager::getLastBackup(std::vector<uint8_t>& out) const {
  if (_lastBackup.empty()) return false;
  out = _lastBackup;
  return true;
}

uint32_t BackupManager::plannedSecondsAt(uint32_t baud) const {
  // Conservative estimate (wire overhead + prompt delays)
  double bytesOnWire = (double)_plannedBytes * 3.6;
  double bps = (double)baud / 10.0;
  double sec = bytesOnWire / (bps > 1 ? bps : 1);
  sec += 5.0 * (double)_ranges.size();
  return (uint32_t)(sec + 0.5);
}

void BackupManager::cancel() {
  if (!_running) return;
  _running = false;
  _status = "cancelled";
  _st = State::Idle;
}

void BackupManager::sendLine(const String& s) {
  if (!_t) return;
  _t->print(s);
  _t->print("\n");
}

void BackupManager::advance(State s, uint32_t timeoutMs, const String& status) {
  _st = s;
  _deadlineMs = millis() + timeoutMs;
  _status = status;
}

void BackupManager::sniffPrompt(uint8_t c) {
  _last2 = _last1;
  _last1 = c;
  if (_last2 == '=' && _last1 == '>') {
    _promptSeen = true;
    _promptLastMs = millis();
    _promptCount++;
  }
}

void BackupManager::onTargetBytes(const uint8_t* data, size_t len) {
  if (!_running) return;

  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    sniffPrompt(c);

    if (_st == State::WaitEnvDone || _st == State::SendPrintenv) {
      _envText += (char)c;
      if (_envText.length() > 96 * 1024) {
        _envText.remove(0, _envText.length() - 96 * 1024);
      }
    }

    if (_st == State::WaitMdData || _st == State::WaitMdPrompt) {
      _hex.feed(&c, 1);
      if (_hex.popBytes(_hexOut)) {
        size_t take = _hexOut.size();
        if (_currentChunkGot + take > _currentChunkBytes) {
          take = (_currentChunkBytes > _currentChunkGot) ? (_currentChunkBytes - _currentChunkGot) : 0;
        }
        if (take) {
          auto& rp = _ranges[_rangeIdx];
          rp.data.insert(rp.data.end(), _hexOut.begin(), _hexOut.begin() + take);
          _currentChunkGot += take;
        }
        _hexOut.clear();
      }
    }
  }
}

bool BackupManager::start(bool uartRawDump) {
  if (_running) return false;
  _uartRawDump = uartRawDump;

  _running = true;
  _progress = 0;
  _status = "starting...";
  _st = State::WaitPrompt;

  _envText = "";
  _lastBackup.clear();

  _ranges.clear();
  _rangeIdx = 0;
  _plannedBytes = 0;

  _promptSeen = false;
  _promptLastMs = 0;
  _promptCount = 0;
  _last1 = _last2 = 0;

  _hex.reset();
  _hexOut.clear();
  _currentChunkBlocks = 0;
  _currentChunkBytes = 0;
  _currentChunkGot = 0;

  advance(State::WaitPrompt, 7000, "waiting for U-Boot prompt (=>)");
  return true;
}

bool BackupManager::planRanges(String* err) {
  std::vector<ProfileRange> planned;

  if (_profileId == "CUSTOM") {
    if (_customCount == 0) { if (err) *err = "CUSTOM range count is 0"; return false; }
    addRange(planned, _customStart, _customCount);
  } else {
    String e;
    if (!appendProfileRanges(_profileId, planned, &e)) {
      if (err) *err = e;
      return false;
    }
    if (planned.empty()) {
      if (err) *err = "Profile produced empty range list";
      return false;
    }
  }

  // Block FULL for raw dumps
  if (_uartRawDump && _profileId == "FULL") {
    if (err) *err = "FULL profile is blocked for UART raw dump";
    return false;
  }

  _ranges.clear();
  _plannedBytes = 0;

  for (size_t i = 0; i < planned.size(); i++) {
    RangePlan rp;
    rp.lba_start   = planned[i].start;
    rp.lba_count   = planned[i].count;
    rp.done_blocks = 0;
    uint64_t bytes = (uint64_t)rp.lba_count * 512ULL;
    _plannedBytes += bytes;
    _ranges.push_back(std::move(rp));
  }

  if (_uartRawDump && _plannedBytes > CFG_BACKUP_MAX_BYTES) {
    if (err) *err = String("Planned backup too large for RAM: ") +
                    (unsigned)(_plannedBytes / 1024 / 1024) +
                    " MiB (cap 8 MiB)";
    return false;
  }

  return true;
}

bool BackupManager::nextChunk(String* err) {
  (void)err;
  if (_rangeIdx >= _ranges.size()) return false;
  auto& rp = _ranges[_rangeIdx];

  if (rp.done_blocks >= rp.lba_count) {
    _rangeIdx++;
    if (_rangeIdx >= _ranges.size()) return false;
    return nextChunk(nullptr);
  }

  uint32_t remaining = rp.lba_count - rp.done_blocks;
  _currentChunkBlocks = remaining > _blocksPerChunk ? _blocksPerChunk : remaining;
  _currentChunkBytes  = (size_t)_currentChunkBlocks * 512u;
  _currentChunkGot    = 0;
  _hex.reset();
  return true;
}

void BackupManager::tick() {
  if (!_running) return;

  if ((int32_t)(millis() - _deadlineMs) > 0) {
    _status = "timeout: " + _status;
    _st = State::Error;
  }

  switch (_st) {
    case State::WaitPrompt: {
      if (_promptSeen && (millis() - _promptLastMs) < 1500) {
        advance(State::SendBanner, 1500, "U-Boot prompt detected");
      }
    } break;

    case State::SendBanner: {
      sendLine("echo K2_UART_BRIDGE_BACKUP");
      advance(State::SendPrintenv, 1500, "requesting printenv");
    } break;

    case State::SendPrintenv: {
      _envText = "";
      sendLine("printenv");
      advance(State::WaitEnvDone, 3500, "capturing printenv");
    } break;

    case State::WaitEnvDone: {
      if (_promptCount >= 2 ||
          (_promptSeen && (millis() - _promptLastMs) < 1500 && _envText.length() > 64)) {
        advance(State::PlanRanges, 1500, "planning ranges");
      }
    } break;

    case State::PlanRanges: {
      String err;
      if (!planRanges(&err)) {
        _status = String("backup failed: ") + err;
        _st = State::Error;
        break;
      }
      _rangeIdx = 0;

      if (!_uartRawDump) {
        advance(State::BuildK2Bak, 3000, "building .k2bak (env+meta)");
      } else {
        if (!nextChunk(nullptr)) {
          advance(State::BuildK2Bak, 3000, "building .k2bak");
        } else {
          advance(State::SendMmcRead, 2500, "reading blocks (mmc read)");
        }
      }
    } break;

    case State::SendMmcRead: {
      if (!_promptSeen || (millis() - _promptLastMs) > 1500) {
        advance(State::WaitPrompt, 7000, "waiting for U-Boot prompt (=>)");
        break;
      }
      auto& rp = _ranges[_rangeIdx];
      uint32_t lba = rp.lba_start + rp.done_blocks;
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "mmc read ${loadaddr} 0x%lX 0x%lX",
               (unsigned long)lba, (unsigned long)_currentChunkBlocks);
      sendLine(cmd);
      advance(State::WaitMmcReadPrompt, 7000, "waiting mmc read to finish");
    } break;

    case State::WaitMmcReadPrompt: {
      if (_promptSeen && (millis() - _promptLastMs) < 1500) {
        advance(State::SendMd, 2000, "dumping memory (md.b)");
      }
    } break;

    case State::SendMd: {
      _hex.reset();
      _currentChunkGot = 0;
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "md.b ${loadaddr} 0x%lX",
               (unsigned long)_currentChunkBytes);
      sendLine(cmd);
      advance(State::WaitMdData, 12000, "parsing md.b hex");
    } break;

    case State::WaitMdData: {
      if (_currentChunkGot >= _currentChunkBytes) {
        advance(State::WaitMdPrompt, 7000, "waiting md.b prompt");
      } else {
        uint64_t done = 0;
        for (size_t i = 0; i < _ranges.size(); i++) done += (uint64_t)_ranges[i].data.size();
        _progress = (_plannedBytes > 0) ? (float)((double)done / (double)_plannedBytes) : 0.0f;
      }
    } break;

    case State::WaitMdPrompt: {
      if (_promptSeen && (millis() - _promptLastMs) < 1500) {
        auto& rp = _ranges[_rangeIdx];
        rp.done_blocks += _currentChunkBlocks;

        if (nextChunk(nullptr)) {
          advance(State::SendMmcRead, 2500, "reading blocks (mmc read)");
        } else {
          advance(State::BuildK2Bak, 5000, "building .k2bak");
        }
      }
    } break;

    case State::BuildK2Bak: {
      std::vector<K2Bak::Range> ranges;
      ranges.reserve(_ranges.size());
      for (size_t i = 0; i < _ranges.size(); i++) {
        K2Bak::Range r;
        r.lba_start = _ranges[i].lba_start;
        r.lba_count = _ranges[i].lba_count;
        r.flags     = K2Bak::RANGE_RAW;
        if (_uartRawDump) r.data = std::move(_ranges[i].data);
        ranges.push_back(std::move(r));
      }

      const String boardId = inferBoardIdFromEnv(_envText);
      const uint64_t ts = (uint64_t)(millis() / 1000ULL);

      std::vector<uint8_t> out;
      String err;
      bool ok = K2Bak::buildV2(out, boardId, _profileId, ts, _envText, ranges, &err);

      if (!ok) {
        _status = String("backup failed: ") + err;
        backup_logf("[BACKUP] %s\n", _status.c_str());
        _st = State::Error;
        break;
      }

      _lastBackup = std::move(out);
      _progress = 1.0f;
      _status = _uartRawDump ? "backup ready (.k2bak with payload)" : "backup ready (.k2bak / env+meta only)";
      backup_logf("[BACKUP] done size=%u bytes\n", (unsigned)_lastBackup.size());
      _st = State::Done;
      _running = false;
    } break;

    case State::Done: {
      _running = false;
    } break;

    case State::Error: {
      backup_logf("[BACKUP] ERROR: %s\n", _status.c_str());
      _running = false;
      _st = State::Idle;
    } break;

    default: break;
  }
}

String BackupManager::inferBoardIdFromEnv(const String& env) const {
  auto findVal = [&](const char* key) -> String {
    int idx = env.indexOf(String(key) + "=");
    if (idx < 0) return "";
    int s = idx + String(key).length() + 1;
    int e = env.indexOf('\n', s);
    if (e < 0) e = env.length();
    String v = env.substring(s, e);
    v.trim();
    return v;
  };

  String v;
  v = findVal("serial#");     if (v.length()) return String("serial#=") + v;
  v = findVal("chipid");      if (v.length()) return String("chipid=") + v;
  v = findVal("board_name");  if (v.length()) return String("board_name=") + v;
  v = findVal("board");       if (v.length()) return String("board=") + v;
  v = findVal("ethaddr");     if (v.length()) return String("ethaddr=") + v;
  v = findVal("wlanaddr");    if (v.length()) return String("wlanaddr=") + v;
  v = findVal("wifiaddr");    if (v.length()) return String("wifiaddr=") + v;

  uint32_t h = 2166136261u;
  for (size_t i = 0; i < (size_t)env.length(); i++) {
    h ^= (uint8_t)env[i];
    h *= 16777619u;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "unknown_%08X", (unsigned)h);
  return String(buf);
}