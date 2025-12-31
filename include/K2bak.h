#pragma once
#include <Arduino.h>
#include "AppConfig.h"
#include "Debug.h"
#include <vector>

// Container format versions (single-source in AppConfig.h)

// ============================================================
// K2BAK container (single-file backup)
//
// v2 (current):
//   HeaderV2 + board_id + profile_id + env + range table + payload blobs + FooterV2
//   - file_crc32 validates integrity (fast)
//   - footer.sha256 validates integrity (strong)
//
// v1 (legacy / pre-task-B drafts):
//   HeaderV1 + board_id + env + range table + payload blobs
//   - file_crc32 + per-range crc32
// ============================================================

namespace K2Bak {

// v2 magic (5 bytes) "K2BAK"
static constexpr uint8_t  MAGIC5[5] = { 'K','2','B','A','K' };

enum FileFlags : uint32_t {
  FLAG_NONE          = 0,
  FLAG_HAS_BOARD_ID  = 1u << 0,
  FLAG_HAS_ENV_TEXT  = 1u << 1,
  FLAG_HAS_RANGES    = 1u << 2,
  FLAG_HAS_PROFILE_ID= 1u << 3,
};

enum RangeFlags : uint32_t {
  RANGE_RAW          = 1u << 0,
  // future: RANGE_COMPRESSED = 1u << 1,
};

#pragma pack(push, 1)
// ---------------- v1 ----------------
struct HeaderV1 {
  uint8_t  magic[8];
  uint8_t  version;
  uint8_t  reserved0[3];
  uint32_t header_size;
  uint32_t flags;

  uint32_t board_id_len;
  uint32_t env_len;
  uint32_t range_count;

  uint32_t range_table_off;
  uint32_t payload_off;

  uint32_t file_crc32;
};

// ---------------- v2 ----------------
struct HeaderV2 {
  uint8_t  magic[5];         // "K2BAK"
  uint8_t  version;          // 2
  uint8_t  reserved0[2];
  uint32_t header_size;
  uint32_t flags;

  uint64_t timestamp_unix;   // seconds since epoch

  uint32_t board_id_len;
  uint32_t profile_id_len;
  uint32_t env_len;
  uint32_t range_count;

  uint32_t range_table_off;
  uint32_t payload_off;
  uint32_t footer_off;

  uint32_t file_crc32;       // CRC32 of entire file with this field zeroed during calc
};

struct RangeEntry {
  uint32_t lba_start;
  uint32_t lba_count;

  uint32_t data_off;         // offset from file start
  uint32_t data_len;         // bytes

  uint32_t crc32;            // CRC32 of payload
  uint32_t flags;            // RangeFlags
};

struct FooterV2 {
  uint8_t magic[5];          // "K2END"
  uint8_t reserved0[3];
  uint8_t sha256[32];        // SHA-256 of entire file with header.file_crc32=0 and this sha256 zeroed
};
#pragma pack(pop)

struct Range {
  uint32_t lba_start = 0;
  uint32_t lba_count = 0;
  std::vector<uint8_t> data;   // raw bytes (may be empty for “metadata-only”)
  uint32_t flags = RANGE_RAW;
};

// -------- CRC32 / SHA256 --------
uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed = 0xFFFFFFFFu);
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);
bool sha256(const uint8_t* data, size_t len, uint8_t out32[32]);

// -------- Build / Parse --------
// Build a v2 container.
// NOTE: ranges[i].data may be empty (metadata-only) if the capture method isn't implemented yet.
bool buildV2(
  std::vector<uint8_t>& outFile,
  const String& boardId,
  const String& profileId,
  uint64_t timestampUnix,
  const String& envText,
  const std::vector<Range>& ranges,
  String* err = nullptr
);

struct Parsed {
  // Normalized header info (supports both v1 and v2)
  uint8_t version = 0;
  uint32_t flags = 0;
  uint64_t timestamp_unix = 0;

  String boardId;
  String profileId;
  String envText;
  std::vector<RangeEntry> entries;

  // for reading payloads without copying everything first
  const uint8_t* fileBase = nullptr;
  size_t fileLen = 0;
};

bool parse(
  Parsed& out,
  const uint8_t* fileData,
  size_t fileLen,
  String* err = nullptr
);

bool validateRanges(
  const Parsed& p,
  String* err = nullptr
);

// Convenience: fetch a range payload pointer/len
bool getRangePayload(
  const Parsed& p,
  size_t index,
  const uint8_t*& data,
  size_t& len,
  String* err = nullptr
);

} // namespace K2Bak
