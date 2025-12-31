// ============================================================
// restore_manager.cpp (FULL COPY/PASTE)
// Fixes:
// 1) K2Bak::Parsed uses "entries" (not ranges)
// 2) K2Bak::RangeEntry payload is NOT stored as .data vector
//    It is referenced by (data_off, data_len) into the original file buffer
// 3) Verify engine uses the original file buffer for expected CRC slices
// ============================================================

#include "Restore_manager.h"
#include "Env_parse.h"
#include "Debug.h"

#include <ArduinoJson.h>
#include <math.h>
#include <algorithm>

DBG_REGISTER_MODULE(__FILE__);

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static inline bool hasPayload(const K2Bak::RangeEntry& e) {
  return (e.data_len > 0);
}

// ------------------------------------------------------------
// Basic API
// ------------------------------------------------------------

void RestoreManager::begin(HardwareSerial* target){
  _t = target;
  DBG_PRINTF("[RESTORE] begin\n");
}

bool RestoreManager::loadBackupFile(const uint8_t* buf, size_t len){
  _loaded = false;
  _lastErr = "";

  // Keep reference to the raw uploaded file buffer (so verify can read payload slices)
  _filePtr = buf;
  _fileLen = len;

  String err;
  K2Bak::Parsed p;
  if (!K2Bak::parse(p, buf, len, &err)) {
    _lastErr = err;
    DBG_PRINTF("[RESTORE] parse failed: %s\n", _lastErr.c_str());
    return false;
  }

  if (!K2Bak::validateRanges(p, &err)) {
    _lastErr = err;
    DBG_PRINTF("[RESTORE] range validation failed: %s\n", _lastErr.c_str());
    return false;
  }

  _p = std::move(p);
  _loaded = true;

  DBG_PRINTF("[RESTORE] loaded ok (ver=%u entries=%u)\n",
             (unsigned)_p.version, (unsigned)_p.entries.size());
  return true;
}

String RestoreManager::getEnvText() const{
  if (!_loaded) return "";
  return _p.envText;
}

String RestoreManager::getSummaryJson() const {
  if (!_loaded) return String("{\"loaded\":false,\"error\":\"") + _lastErr + "\"}";

  JsonDocument d;
  d["loaded"] = true;
  d["version"] = _p.version;
  d["timestamp_unix"] = (uint64_t)_p.timestamp_unix;
  d["board_id"] = _p.boardId;
  d["profile_id"] = _p.profileId;
  d["range_count"] = (uint32_t)_p.entries.size();

  JsonArray a = d["ranges"].to<JsonArray>();
  for (size_t i = 0; i < _p.entries.size(); i++) {
    const auto& e = _p.entries[i];
    JsonObject r = a.add<JsonObject>();
    r["index"]     = (uint32_t)i;
    r["lba_start"] = e.lba_start;
    r["lba_count"] = e.lba_count;
    r["data_len"]  = e.data_len;
    r["data_off"]  = e.data_off;
    r["flags"]     = e.flags;
  }

  String out;
  serializeJsonPretty(d, out);
  return out;
}

bool RestoreManager::fileRequiresFullConfirm() const {
  if (!_loaded) return false;
  return _p.profileId.equalsIgnoreCase("FULL");
}

bool RestoreManager::checkBoardIdMatches(const String& currentBoardId, String* whyNot) const {
  if (!_loaded) {
    if (whyNot) *whyNot = "No restore file loaded";
    return false;
  }
  if (_p.boardId.length() == 0 || _p.boardId.startsWith("unknown_")) {
    if (whyNot) *whyNot = "Backup file board_id is unknown; cannot safely match";
    return false;
  }
  if (currentBoardId.length() == 0) {
    if (whyNot) *whyNot = "Current board_id unknown";
    return false;
  }
  if (_p.boardId != currentBoardId) {
    if (whyNot) *whyNot = String("board_id mismatch: file=") + _p.boardId + " current=" + currentBoardId;
    return false;
  }
  return true;
}

uint64_t RestoreManager::getTotalRangeBytes() const {
  if (!_loaded) return 0;
  uint64_t bytes = 0;
  for (auto &e : _p.entries) {
    bytes += (uint64_t)e.lba_count * 512ULL;
  }
  return bytes;
}

String RestoreManager::getLayoutHintJson() const {
  if (!_loaded) return "{}";
  return EnvParse::layoutHintJson(_p.envText);
}

// ============================================================
// Task D: Verify engine (read device ranges over UART and compare CRC)
// ============================================================

static uint32_t g_crc_table[256];
static bool g_crc_init = false;
static void init_crc_tbl() {
  if (g_crc_init) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int j = 0; j < 8; j++) {
      if (c & 1) c = 0xEDB88320u ^ (c >> 1);
      else       c = c >> 1;
    }
    g_crc_table[i] = c;
  }
  g_crc_init = true;
}

uint32_t RestoreManager::crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  init_crc_tbl();
  uint32_t c = crc;
  for (size_t i = 0; i < len; i++) {
    c = g_crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  }
  return c;
}

void RestoreManager::sniffPrompt(uint8_t c){
  _last2 = _last1;
  _last1 = c;
  if (_last2 == '=' && _last1 == '>') {
    _promptSeen = true;
    _promptLastMs = millis();
    _promptCount++;
  }
}

void RestoreManager::onTargetBytes(const uint8_t* data, size_t len){
  if(!_verifying) return;
  for(size_t i=0;i<len;i++){
    uint8_t c = data[i];
    sniffPrompt(c);

    if (_vs == VState::WaitMdData || _vs == VState::WaitMdPrompt) {
      _hex.feed(&c, 1);
      if (_hex.popBytes(_hexOut)) {
        size_t take = _hexOut.size();
        if (_chunkGot + take > _chunkBytes) {
          take = (_chunkBytes > _chunkGot) ? (_chunkBytes - _chunkGot) : 0;
        }
        if (take) {
          _crc = crc32_update(_crc, _hexOut.data(), take);
          _chunkGot += take;
        }
        _hexOut.clear();
      }
    }
  }
}

bool RestoreManager::startVerify(){
  if(!_loaded) { _vStatus="No restore file loaded"; return false; }
  if(_p.entries.empty()) { _vStatus="No ranges in file"; return false; }
  if(!_filePtr || _fileLen == 0) { _vStatus="No file buffer available"; return false; }

  // Must have payload for meaningful verify
  bool anyPayload=false;
  for(auto &e:_p.entries) { if(hasPayload(e)){ anyPayload=true; break; } }
  if(!anyPayload) { _vStatus="Verify requires payload ranges (.k2bak meta-only)"; return false; }

  _verifying = true;
  _vProgress = 0;
  _vStatus = "waiting for U-Boot prompt (=>)";
  _vs = VState::WaitPrompt;
  _deadlineMs = millis() + 7000;

  _promptSeen = false;
  _promptLastMs = 0;
  _promptCount = 0;
  _last1 = _last2 = 0;

  _hex.reset();
  _hexOut.clear();

  _rangeIdx = 0;
  _doneBlocks = 0;

  return true;
}

void RestoreManager::tick(){
  if(!_verifying) return;

  if ((int32_t)(millis() - _deadlineMs) > 0) {
    _vStatus = "timeout: " + _vStatus;
    _vs = VState::Error;
  }

  // Guard
  if (_rangeIdx >= _p.entries.size()) {
    _vStatus = "verify OK";
    _vProgress = 1.0f;
    _verifying = false;
    _vs = VState::Done;
    return;
  }

  auto &R = _p.entries[_rangeIdx];

  // If this entry has no payload, skip it (verify only makes sense for payload entries)
  if (!hasPayload(R)) {
    _rangeIdx++;
    _doneBlocks = 0;
    _vStatus = "skipping non-payload range";
    _vs = VState::WaitPrompt;
    _deadlineMs = millis() + 7000;
    return;
  }

  auto startNextChunk = [&](){
    uint32_t remaining = (R.lba_count - _doneBlocks);
    uint32_t blocks = remaining > _chunkBlocks ? _chunkBlocks : remaining;
    _chunkBytes = (size_t)blocks * 512u;
    _chunkGot = 0;
    _crc = 0xFFFFFFFFu;
    _hex.reset();
    return blocks;
  };

  switch(_vs){
    case VState::WaitPrompt: {
      if(_promptSeen && (millis()-_promptLastMs) < 1500){
        _vs = VState::SendMmcRead;
        _deadlineMs = millis() + 2500;
        _vStatus = "verifying: mmc read";
      }
    } break;

    case VState::SendMmcRead: {
      if(!_promptSeen || (millis()-_promptLastMs) > 1500){
        _vs = VState::WaitPrompt;
        _deadlineMs = millis() + 7000;
        _vStatus = "waiting for U-Boot prompt (=>)";
        break;
      }
      uint32_t blocks = startNextChunk();
      uint32_t lba = R.lba_start + _doneBlocks;
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "mmc read ${loadaddr} 0x%lX 0x%lX",
               (unsigned long)lba, (unsigned long)blocks);
      _t->print(cmd); _t->print("\n");
      _vs = VState::WaitReadPrompt;
      _deadlineMs = millis() + 7000;
      _vStatus = "verifying: wait read prompt";
    } break;

    case VState::WaitReadPrompt: {
      if(_promptSeen && (millis()-_promptLastMs) < 1500){
        _vs = VState::SendMd;
        _deadlineMs = millis() + 2000;
        _vStatus = "verifying: md.b";
      }
    } break;

    case VState::SendMd: {
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "md.b ${loadaddr} 0x%lX", (unsigned long)_chunkBytes);
      _t->print(cmd); _t->print("\n");
      _vs = VState::WaitMdData;
      _deadlineMs = millis() + 14000;
      _vStatus = "verifying: parsing hex";
    } break;

    case VState::WaitMdData: {
      if(_chunkGot >= _chunkBytes){
        _vs = VState::WaitMdPrompt;
        _deadlineMs = millis() + 7000;
        _vStatus = "verifying: wait prompt";
      } else {
        // progress by blocks (all entries)
        uint64_t totalBlocks=0, doneBlocks=0;
        for(size_t i=0;i<_p.entries.size();i++){
          totalBlocks += _p.entries[i].lba_count;
          if(i<_rangeIdx) doneBlocks += _p.entries[i].lba_count;
        }
        doneBlocks += _doneBlocks;
        _vProgress = totalBlocks ? (float)((double)doneBlocks / (double)totalBlocks) : 0.0f;
      }
    } break;

    case VState::WaitMdPrompt: {
      if(_promptSeen && (millis()-_promptLastMs) < 1500){
        uint32_t chunkCrc = (_crc ^ 0xFFFFFFFFu);

        // Expected CRC from file payload slice
        uint32_t blocks = (uint32_t)(_chunkBytes / 512u);

        // Chunk offset within this entry's payload:
        size_t chunkOff = (size_t)_doneBlocks * 512u;
        if (chunkOff + _chunkBytes > (size_t)R.data_len) {
          _vStatus = "verify failed: chunk beyond entry payload length";
          _vs = VState::Error;
          break;
        }

        // Location in the .k2bak file buffer:
        size_t fileOff = (size_t)R.data_off + chunkOff;
        if (fileOff + _chunkBytes > _fileLen) {
          _vStatus = "verify failed: payload beyond file buffer";
          _vs = VState::Error;
          break;
        }

        const uint8_t* expPtr = _filePtr + fileOff;

        uint32_t exp = 0xFFFFFFFFu;
        exp = crc32_update(exp, expPtr, _chunkBytes);
        exp ^= 0xFFFFFFFFu;

        if (chunkCrc != exp) {
          char buf[180];
          snprintf(buf, sizeof(buf),
                   "verify mismatch @range%u lba=0x%lX blocks=0x%lX",
                   (unsigned)_rangeIdx,
                   (unsigned long)(R.lba_start + _doneBlocks),
                   (unsigned long)blocks);
          _vStatus = String("verify failed: ") + buf;
          _vs = VState::Error;
          break;
        }

        _doneBlocks += blocks;

        if(_doneBlocks >= R.lba_count){
          _rangeIdx++;
          _doneBlocks = 0;

          if(_rangeIdx >= _p.entries.size()){
            _vStatus = "verify OK";
            _vProgress = 1.0f;
            _vs = VState::Done;
            _verifying = false;
          } else {
            _vStatus = "verifying next range";
            _vs = VState::WaitPrompt;
            _deadlineMs = millis() + 7000;
          }
        } else {
          _vs = VState::SendMmcRead;
          _deadlineMs = millis() + 2500;
          _vStatus = "verifying: next chunk";
        }
      }
    } break;

    case VState::Done: {
      _verifying = false;
    } break;

    case VState::Error: {
      DBG_PRINTF("[RESTORE] VERIFY ERROR: %s\n", _vStatus.c_str());
      _verifying = false;
      _vs = VState::Idle;
    } break;

    default: break;
  }
}