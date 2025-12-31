#include "K2bak.h"

#include <mbedtls/sha256.h>
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

namespace K2Bak {

// ============================================================
// CRC32 (standard polynomial 0xEDB88320)
// ============================================================

static uint32_t crc_table[256];
static bool crc_init = false;

static void init_crc() {
  if (crc_init) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) {
      c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    crc_table[i] = c;
  }
  crc_init = true;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  init_crc();
  uint32_t c = crc;
  for (size_t i = 0; i < len; i++) {
    c = crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  }
  return c;
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed) {
  uint32_t c = crc32_update(seed, data, len);
  return c ^ 0xFFFFFFFFu;
}

bool sha256(const uint8_t* data, size_t len, uint8_t out32[32]) {
  if (!data || !out32) return false;
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  if (mbedtls_sha256_update_ret(&ctx, data, len) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  if (mbedtls_sha256_finish_ret(&ctx, out32) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  mbedtls_sha256_free(&ctx);
  return true;
}

// ============================================================
// Helpers
// ============================================================

static void write_bytes(std::vector<uint8_t>& v, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  v.insert(v.end(), b, b + n);
}

static bool in_bounds(size_t off, size_t len, size_t fileLen) {
  if (off > fileLen) return false;
  if (len > fileLen) return false;
  if (off + len < off) return false; // overflow
  if (off + len > fileLen) return false;
  return true;
}

static void memzero(uint8_t* p, size_t n) {
  if (!p || !n) return;
  for (size_t i = 0; i < n; i++) p[i] = 0;
}

// ============================================================
// Build (v2)
// ============================================================

bool buildV2(
  std::vector<uint8_t>& outFile,
  const String& boardId,
  const String& profileId,
  uint64_t timestampUnix,
  const String& envText,
  const std::vector<Range>& ranges,
  String* err
) {
  outFile.clear();

  const bool hasBoard   = boardId.length() > 0;
  const bool hasProfile = profileId.length() > 0;
  const bool hasEnv     = envText.length() > 0;
  const bool hasRanges  = ranges.size() > 0;

  HeaderV2 h{};
  memcpy(h.magic, MAGIC5, sizeof(MAGIC5));
  h.version      = CFG_K2BAK_VERSION_V2;
  h.header_size  = (uint32_t)sizeof(HeaderV2);
  h.flags        = FLAG_NONE;
  if (hasBoard)   h.flags |= FLAG_HAS_BOARD_ID;
  if (hasProfile) h.flags |= FLAG_HAS_PROFILE_ID;
  if (hasEnv)     h.flags |= FLAG_HAS_ENV_TEXT;
  if (hasRanges)  h.flags |= FLAG_HAS_RANGES;

  h.timestamp_unix = timestampUnix;
  h.board_id_len   = (uint32_t)boardId.length();
  h.profile_id_len = (uint32_t)profileId.length();
  h.env_len        = (uint32_t)envText.length();
  h.range_count    = (uint32_t)ranges.size();

  h.range_table_off = 0;
  h.payload_off     = 0;
  h.footer_off      = 0;
  h.file_crc32      = 0;

  // 1) header placeholder
  write_bytes(outFile, &h, sizeof(h));

  // 2) board id
  if (hasBoard) write_bytes(outFile, boardId.c_str(), boardId.length());

  // 3) profile id
  if (hasProfile) write_bytes(outFile, profileId.c_str(), profileId.length());

  // 4) env text
  if (hasEnv) write_bytes(outFile, envText.c_str(), envText.length());

  // 5) range table
  h.range_table_off = (uint32_t)outFile.size();
  std::vector<RangeEntry> table;
  table.resize(ranges.size());
  write_bytes(outFile, table.data(), table.size() * sizeof(RangeEntry));

  // 6) payloads
  h.payload_off = (uint32_t)outFile.size();
  for (size_t i = 0; i < ranges.size(); i++) {
    RangeEntry e{};
    e.lba_start = ranges[i].lba_start;
    e.lba_count = ranges[i].lba_count;
    e.flags     = ranges[i].flags;
    e.data_off  = (uint32_t)outFile.size();
    e.data_len  = (uint32_t)ranges[i].data.size();

    if (!ranges[i].data.empty()) {
      e.crc32 = crc32(ranges[i].data.data(), ranges[i].data.size());
      write_bytes(outFile, ranges[i].data.data(), ranges[i].data.size());
    } else {
      e.crc32 = 0;
    }
    table[i] = e;
  }

  // 7) footer placeholder
  h.footer_off = (uint32_t)outFile.size();
  FooterV2 f{};
  const uint8_t endMagic[5] = { 'K','2','E','N','D' };
  memcpy(f.magic, endMagic, sizeof(endMagic));
  memzero(f.sha256, sizeof(f.sha256));
  write_bytes(outFile, &f, sizeof(f));

  // 8) patch header + table
  memcpy(outFile.data(), &h, sizeof(h));
  if (!table.empty()) {
    memcpy(outFile.data() + h.range_table_off, table.data(), table.size() * sizeof(RangeEntry));
  }

  // 9) compute integrity
  // CRC32 of entire file with header.file_crc32 treated as 0.
  // SHA256 of entire file with header.file_crc32=0 and footer.sha256=0.
  std::vector<uint8_t> tmp(outFile);
  HeaderV2 hz = h;
  hz.file_crc32 = 0;
  memcpy(tmp.data(), &hz, sizeof(hz));
  // zero sha in temp
  if (h.footer_off + sizeof(FooterV2) <= tmp.size()) {
    FooterV2 fz{};
    memcpy(&fz, tmp.data() + h.footer_off, sizeof(FooterV2));
    memzero(fz.sha256, sizeof(fz.sha256));
    memcpy(tmp.data() + h.footer_off, &fz, sizeof(FooterV2));
  }

  const uint32_t fileCrc = crc32(tmp.data(), tmp.size());
  uint8_t fileSha[32];
  if (!sha256(tmp.data(), tmp.size(), fileSha)) {
    if (err) *err = "SHA256 failed";
    return false;
  }

  // patch real CRC in header
  h.file_crc32 = fileCrc;
  memcpy(outFile.data(), &h, sizeof(h));

  // patch SHA in footer
  FooterV2 fout{};
  memcpy(&fout, outFile.data() + h.footer_off, sizeof(FooterV2));
  memcpy(fout.sha256, fileSha, sizeof(fileSha));
  memcpy(outFile.data() + h.footer_off, &fout, sizeof(FooterV2));

  return true;
}

// ============================================================
// Parse (v1 + v2)
// ============================================================

static bool parse_v2(Parsed& out, const uint8_t* fileData, size_t fileLen, String* err) {
  if (!fileData || fileLen < sizeof(HeaderV2) + sizeof(FooterV2)) {
    if (err) *err = "File too small";
    return false;
  }

  HeaderV2 h{};
  memcpy(&h, fileData, sizeof(h));

  if (memcmp(h.magic, MAGIC5, sizeof(MAGIC5)) != 0) {
    if (err) *err = "Bad magic (not a .k2bak file)";
    return false;
  }
  if (h.version != CFG_K2BAK_VERSION_V2) {
    if (err) *err = String("Unsupported version: ") + h.version;
    return false;
  }
  if (h.header_size != sizeof(HeaderV2)) {
    if (err) *err = "Header size mismatch";
    return false;
  }

  // bounds sanity
  if (!in_bounds(0, sizeof(HeaderV2), fileLen)) { if (err) *err = "Header out of bounds"; return false; }
  if (!in_bounds(h.footer_off, sizeof(FooterV2), fileLen)) { if (err) *err = "Footer out of bounds"; return false; }

  // read variable fields
  size_t off = sizeof(HeaderV2);
  if (h.board_id_len) {
    if (!in_bounds(off, h.board_id_len, fileLen)) { if (err) *err = "board_id out of bounds"; return false; }
    out.boardId = String((const char*)(fileData + off)).substring(0, h.board_id_len);
    off += h.board_id_len;
  }
  if (h.profile_id_len) {
    if (!in_bounds(off, h.profile_id_len, fileLen)) { if (err) *err = "profile_id out of bounds"; return false; }
    out.profileId = String((const char*)(fileData + off)).substring(0, h.profile_id_len);
    off += h.profile_id_len;
  }
  if (h.env_len) {
    if (!in_bounds(off, h.env_len, fileLen)) { if (err) *err = "env out of bounds"; return false; }
    out.envText = String((const char*)(fileData + off)).substring(0, h.env_len);
    off += h.env_len;
  }

  // Range table
  const size_t tableBytes = (size_t)h.range_count * sizeof(RangeEntry);
  if (h.range_count) {
    if (!in_bounds(h.range_table_off, tableBytes, fileLen)) { if (err) *err = "range_table out of bounds"; return false; }
    out.entries.resize(h.range_count);
    memcpy(out.entries.data(), fileData + h.range_table_off, tableBytes);
  }

  // footer + integrity check
  FooterV2 f{};
  memcpy(&f, fileData + h.footer_off, sizeof(f));
  const uint8_t endMagic[5] = { 'K','2','E','N','D' };
  if (memcmp(f.magic, endMagic, sizeof(endMagic)) != 0) {
    if (err) *err = "Bad footer magic";
    return false;
  }

  // Prepare temp with crc+sha zeroed and compute
  std::vector<uint8_t> tmp(fileData, fileData + fileLen);
  HeaderV2 hz = h;
  hz.file_crc32 = 0;
  memcpy(tmp.data(), &hz, sizeof(hz));
  FooterV2 fz = f;
  memzero(fz.sha256, sizeof(fz.sha256));
  memcpy(tmp.data() + h.footer_off, &fz, sizeof(fz));

  const uint32_t wantCrc = h.file_crc32;
  const uint32_t gotCrc  = crc32(tmp.data(), tmp.size());
  if (wantCrc != gotCrc) {
    if (err) *err = "File CRC mismatch (corrupt backup file)";
    return false;
  }

  uint8_t gotSha[32];
  if (!sha256(tmp.data(), tmp.size(), gotSha)) {
    if (err) *err = "SHA256 failed";
    return false;
  }
  if (memcmp(gotSha, f.sha256, 32) != 0) {
    if (err) *err = "File SHA256 mismatch (corrupt backup file)";
    return false;
  }

  // normalize output header fields
  out.version = CFG_K2BAK_VERSION_V2;
  out.flags = h.flags;
  out.timestamp_unix = h.timestamp_unix;
  out.fileBase = fileData;
  out.fileLen = fileLen;
  return true;
}

static bool parse_v1(Parsed& out, const uint8_t* fileData, size_t fileLen, String* err) {
  // Legacy: magic[8] = { 'K','2','B','A','K',0,0,1 }
  if (!fileData || fileLen < sizeof(HeaderV1)) {
    if (err) *err = "File too small";
    return false;
  }

  HeaderV1 h{};
  memcpy(&h, fileData, sizeof(h));

  const uint8_t legacyMagic[8] = { 'K','2','B','A','K', 0x00, 0x00, 0x01 };
  if (memcmp(h.magic, legacyMagic, sizeof(legacyMagic)) != 0) {
    if (err) *err = "Bad magic (not a legacy .k2bak v1 file)";
    return false;
  }
  if (h.version != CFG_K2BAK_VERSION_V1) {
    if (err) *err = String("Unsupported version: ") + h.version;
    return false;
  }
  if (h.header_size != sizeof(HeaderV1)) {
    if (err) *err = "Header size mismatch";
    return false;
  }

  // variable fields
  size_t off = sizeof(HeaderV1);
  if (h.board_id_len) {
    if (!in_bounds(off, h.board_id_len, fileLen)) { if (err) *err="board_id out of bounds"; return false; }
    out.boardId = String((const char*)(fileData + off)).substring(0, h.board_id_len);
    off += h.board_id_len;
  }
  if (h.env_len) {
    if (!in_bounds(off, h.env_len, fileLen)) { if (err) *err="env out of bounds"; return false; }
    out.envText = String((const char*)(fileData + off)).substring(0, h.env_len);
    off += h.env_len;
  }

  const size_t tableBytes = (size_t)h.range_count * sizeof(RangeEntry);
  if (h.range_count) {
    if (!in_bounds(h.range_table_off, tableBytes, fileLen)) { if (err) *err="range_table out of bounds"; return false; }
    out.entries.resize(h.range_count);
    memcpy(out.entries.data(), fileData + h.range_table_off, tableBytes);
  }

  // CRC check (treat header.file_crc32 as 0)
  std::vector<uint8_t> tmp(fileData, fileData + fileLen);
  HeaderV1 hz = h;
  hz.file_crc32 = 0;
  memcpy(tmp.data(), &hz, sizeof(hz));

  const uint32_t want = h.file_crc32;
  const uint32_t got  = crc32(tmp.data(), tmp.size());
  if (want != got) {
    if (err) *err = "File CRC mismatch (corrupt backup file)";
    return false;
  }

  out.version = CFG_K2BAK_VERSION_V1;
  out.flags = h.flags;
  out.timestamp_unix = 0;
  out.profileId = "";
  out.fileBase = fileData;
  out.fileLen = fileLen;
  return true;
}

bool parse(Parsed& out, const uint8_t* fileData, size_t fileLen, String* err) {
  out = Parsed{};
  out.fileBase = fileData;
  out.fileLen  = fileLen;
  if (!fileData || fileLen < 8) {
    if (err) *err = "File too small";
    return false;
  }
  // Detect v2 by leading "K2BAK" and version byte
  if (fileLen >= sizeof(HeaderV2) &&
      memcmp(fileData, MAGIC5, sizeof(MAGIC5)) == 0 &&
      fileData[5] == CFG_K2BAK_VERSION_V2) {
    return parse_v2(out, fileData, fileLen, err);
  }
  return parse_v1(out, fileData, fileLen, err);
}

bool validateRanges(const Parsed& p, String* err) {
  for (size_t i = 0; i < p.entries.size(); i++) {
    const RangeEntry& e = p.entries[i];
    if (e.data_len == 0) continue; // metadata-only ok
    if (!in_bounds(e.data_off, e.data_len, p.fileLen)) {
      if (err) *err = String("Range payload out of bounds at index ") + i;
      return false;
    }
    const uint8_t* data = p.fileBase + e.data_off;
    uint32_t got = crc32(data, e.data_len);
    if (got != e.crc32) {
      if (err) *err = String("Range CRC mismatch at index ") + i;
      return false;
    }
  }
  return true;
}

bool getRangePayload(const Parsed& p, size_t index, const uint8_t*& data, size_t& len, String* err) {
  data = nullptr;
  len = 0;
  if (index >= p.entries.size()) {
    if (err) *err = "Index out of range";
    return false;
  }
  const RangeEntry& e = p.entries[index];
  if (!in_bounds(e.data_off, e.data_len, p.fileLen)) {
    if (err) *err = "Payload out of bounds";
    return false;
  }
  data = p.fileBase + e.data_off;
  len  = e.data_len;
  return true;
}

} // namespace K2Bak
