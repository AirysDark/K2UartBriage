#include "Uboot_hex_parser.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static int hexval(char c){
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

void UBootHexParser::reset(){
  _lineBuf = "";
  _pending.clear();
  _pending.shrink_to_fit(); // optional: remove if you want to keep capacity
}

void UBootHexParser::feed(const uint8_t* data, size_t len){
  if (!data || !len) return;

  for (size_t i = 0; i < len; i++){
    char c = (char)data[i];
    if (c == '\r') continue;

    if (c == '\n'){
      if (_lineBuf.length() > 0) {
        parseLine(_lineBuf);
      }
      _lineBuf = "";
      continue;
    }

    // Use project-wide limit from appconfig.h
    if (_lineBuf.length() < CMD_LINEBUF_MAX) {
      _lineBuf += c;
    }
  }
}

bool UBootHexParser::parseLine(const String& line){
  // Find "ADDR:" prefix
  int colon = line.indexOf(':');
  if (colon < 0) return false;

  // After colon, parse hex byte pairs separated by spaces
  int i = colon + 1;

  while (i < (int)line.length()){
    // skip spaces
    while (i < (int)line.length() && line[i] == ' ') i++;
    if (i + 1 >= (int)line.length()) break;

    int h1 = hexval(line[i]);
    int h2 = hexval(line[i + 1]);

    if (h1 < 0 || h2 < 0) {
      // Stop at ASCII region (or any non-hex)
      break;
    }

    uint8_t b = (uint8_t)((h1 << 4) | h2);
    _pending.push_back(b);

    i += 2;

    // Skip until next space or end (handles "01", "01 " and also weird separators)
    while (i < (int)line.length() && line[i] != ' ') i++;
  }

  return true;
}

bool UBootHexParser::popBytes(std::vector<uint8_t>& out){
  if (_pending.empty()) return false;
  out = std::move(_pending);
  _pending.clear();
  return true;
}
