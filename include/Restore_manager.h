#pragma once
#include "Debug.h"
#include <Arduino.h>
#include <vector>
#include "K2bak.h"
#include "Uboot_hex_parser.h"

class RestoreManager {
public:
  void begin(HardwareSerial* target);

  // File load
  bool loadBackupFile(const uint8_t* buf, size_t len); // parse .k2bak
  bool hasLoaded() const { return _loaded; }
  bool isLoaded() const { return _loaded; }

  String getBoardId() const { return _p.boardId; }
  String getProfileId() const { return _p.profileId; }
  uint64_t getTotalRangeBytes() const;
  String getLayoutHintJson() const;

  String getEnvText() const;
  String getSummaryJson() const;

  // Task B/C safety helpers
  bool fileRequiresFullConfirm() const;
  bool checkBoardIdMatches(const String& currentBoardId, String* whyNot = nullptr) const;

  // Task D: VERIFY engine (read device ranges over UART and compare CRC vs file)
  bool startVerify();
  bool verifying() const { return _verifying; }
  float verifyProgress() const { return _vProgress; }
  String verifyStatus() const { return _vStatus; }

  void onTargetBytes(const uint8_t* data, size_t len);
  void tick();

private:
  HardwareSerial* _t = nullptr;
  bool _loaded = false;

  K2Bak::Parsed _p;
  String _lastErr;

  // Keep reference to raw .k2bak file buffer (payload is at data_off/data_len)
  const uint8_t* _filePtr = nullptr;
  size_t _fileLen = 0;

  // ---- verify state ----
  bool _verifying = false;
  float _vProgress = 0;
  String _vStatus = "idle";

  uint8_t _last1 = 0, _last2 = 0;
  bool _promptSeen = false;
  uint32_t _promptLastMs = 0;
  uint32_t _promptCount = 0;
  void sniffPrompt(uint8_t c);

  UBootHexParser _hex;
  std::vector<uint8_t> _hexOut;

  size_t _rangeIdx = 0;
  uint32_t _doneBlocks = 0;
  uint32_t _chunkBlocks = 64;
  size_t _chunkBytes = 0;
  size_t _chunkGot = 0;
  uint32_t _crc = 0;

  uint32_t _deadlineMs = 0;
  enum class VState : uint8_t { Idle, WaitPrompt, SendMmcRead, WaitReadPrompt, SendMd, WaitMdData, WaitMdPrompt, Next, Done, Error };
  VState _vs = VState::Idle;

  static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);
};
