#pragma once
#include "Debug.h"
#include <Arduino.h>
#include <vector>
#include "Appconfig.h"

// Parses U-Boot md.b style lines like:
// 40010000: 01 02 03 04 05 06 07 08  ....
//
// Accepts CR/LF stream, returns extracted bytes.
class UBootHexParser {
public:
  void reset();
  void feed(const uint8_t* data, size_t len);
  bool popBytes(std::vector<uint8_t>& out); // returns any newly parsed bytes

private:
  String _lineBuf;
  std::vector<uint8_t> _pending;

  bool parseLine(const String& line);
};
