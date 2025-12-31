#pragma once
#include "Debug.h"
#include <Arduino.h>
#include <vector>

struct BackupSection {
  String name;
  std::vector<uint8_t> data;
};

class BackupContainer {
public:
  void clear();
  void add(const String& name, const uint8_t* data, size_t len);
  BackupSection* get(const String& name);

  // serialize into a single file:
  // [magic 'K2BK'][u32 version=1][u32 sectionCount]
  // then for each section:
  // [u16 nameLen][name bytes][u32 dataLen][data bytes]
  bool serialize(std::vector<uint8_t>& out) const;
  bool deserialize(const uint8_t* buf, size_t len);

private:
  std::vector<BackupSection> _sections;
};
