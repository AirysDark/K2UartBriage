#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <vector>

#include "AppConfig.h"
#include "Debug.h"
#include "Backup_profiles.h"
#include "K2bak.h"
#include "Uboot_hex_parser.h"

// Task D: UART raw block dump (U-Boot mmc read + md.b) into .k2bak payload.
// NOTE: This remains RAM-backed; FULL profile is intentionally blocked.
class BackupManager {
public:
  void begin(HardwareSerial* target, Preferences* prefs);

  // profile selection
  void setProfileId(const String& id);
  String getProfileId() const;
  void setCustomRange(uint32_t start, uint32_t count);
  void getCustomRange(uint32_t& start, uint32_t& count) const;

  // start/stop
  bool start(bool uartRawDump = true); // default raw dump; if false -> env+meta only
  bool running() const { return _running; }
  void cancel();

  // drive
  void onTargetBytes(const uint8_t* data, size_t len); // called by main pump
  void tick();

  // status
  float progress() const { return _progress; }
  String statusLine() const { return _status; }

  // output
  bool getLastBackup(std::vector<uint8_t>& out) const;

  // estimates / limits
  uint64_t plannedBytes() const { return _plannedBytes; }
  uint32_t plannedSecondsAt(uint32_t baud) const; // conservative estimate

private:
  HardwareSerial* _t = nullptr;
  Preferences* _prefs = nullptr;

  String _profileId = "A";
  uint32_t _customStart = 0;
  uint32_t _customCount = 0;

  bool _running = false;
  bool _uartRawDump = true;
  float _progress = 0;
  String _status = "idle";

  // output
  std::vector<uint8_t> _lastBackup;

  // ---- UART sniff / prompt detect ----
  uint8_t _last1 = 0, _last2 = 0;
  bool _promptSeen = false;
  uint32_t _promptLastMs = 0;
  uint32_t _promptCount = 0;

  void sniffPrompt(uint8_t c);

  // ---- raw dump engine ----
  struct RangePlan {
    uint32_t lba_start = 0;
    uint32_t lba_count = 0;
    uint32_t done_blocks = 0;
    std::vector<uint8_t> data; // holds full range data (within CFG_BACKUP_MAX_BYTES)
  };

  std::vector<RangePlan> _ranges;
  uint32_t _rangeIdx = 0;

  uint32_t _blocksPerChunk = CFG_BACKUP_DEFAULT_BLOCKS_PER_CHUNK;
  uint32_t _currentChunkBlocks = 0;
  size_t   _currentChunkBytes = 0;
  size_t   _currentChunkGot = 0;

  UBootHexParser _hex;
  std::vector<uint8_t> _hexOut;

  uint64_t _plannedBytes = 0;

  enum class State : uint8_t {
    Idle,
    WaitPrompt,
    SendBanner,
    SendPrintenv,
    WaitEnvDone,
    PlanRanges,
    SendMmcRead,
    WaitMmcReadPrompt,
    SendMd,
    WaitMdData,
    WaitMdPrompt,
    BuildK2Bak,
    Done,
    Error
  };
  State _st = State::Idle;

  // env capture
  String _envText;
  uint32_t _deadlineMs = 0;

  void loadPrefs();
  void savePrefs();

  void sendLine(const String& s);
  void advance(State s, uint32_t timeoutMs, const String& status);

  bool planRanges(String* err);
  bool nextChunk(String* err);

  // best-effort: extract some kind of stable board identifier from printenv
  String inferBoardIdFromEnv(const String& env) const;
};