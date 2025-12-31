#pragma once
#include <Arduino.h>
#include "Debug.h"

class RestorePlan {
public:
  struct Image {
    String filename;           // "payload/kernel"
    String device;             // "/dev/by-name/bootA" (optional for awuboot)
    bool installed_directly;   // true/false
    String type;               // "block" or "awuboot"
  };

  struct BootEnvKV {
    String name;
    String value;
  };

  struct TargetInfo {
    String soc;                // "allwinner-t113"
    String scheme;             // "swupdate-ab"
    String by_name_base;       // "/dev/by-name/"
  };

  bool begin(); // mounts LittleFS if needed (safe to call even if already mounted)

  bool loadFromFile(const char* path);   // loads JSON manifest
  bool isLoaded() const { return _loaded; }

  // Arm/disarm safety gate
  String arm(const String& token, bool overrideBoardId);
  void disarm();
  bool isArmed() const { return _armed; }

  // High level ops
  String planText() const;     // human readable plan
  String verifyText() const;   // checks manifest + files exist (where possible)

  // SAFE by default: returns the commands to run (doesn't auto-write)
  // If you later want ?execute over UART?, you can extend this to send lines.
  String applyText() const;

  // helpers
  const String& profile() const { return _profile; }
  const TargetInfo& target() const { return _target; }

private:
  bool parseJson(const String& json);

  // manifest fields
  String _format;
  int    _version = 0;
  TargetInfo _target;
  String _profile;
  String _notes;

  Image _images[8];
  size_t _imageCount = 0;

  BootEnvKV _bootenv[16];
  size_t _bootenvCount = 0;

  // state
  bool _loaded = false;
  bool _armed = false;
  uint32_t _armedAtMs = 0;

  // policy
  static constexpr uint32_t ARM_TIMEOUT_MS = 5UL * 60UL * 1000UL; // 5 min
};