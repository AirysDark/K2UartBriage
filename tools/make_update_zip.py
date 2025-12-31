#!/usr/bin/env python3
"""Build the K2UartBriage streamed dual-image update container.

Despite the .zip extension, this is a lightweight streaming container so
the ESP32 can flash firmware + LittleFS without needing a zip/unzip library.

Layout (little-endian):
  8 bytes  magic   = b"K2UPD1\0\0"
  4 bytes  fw_size = uint32
  4 bytes  fs_size = uint32
  fw_size  bytes   firmware image (the app .bin)
  fs_size  bytes   littlefs image (.bin produced by buildfs/uploadfs)

Usage:
  # simplest: put firmware.bin and littlefs.bin next to this script, then:
  python tools/make_update_zip.py

  # or pass explicit paths:
  python tools/make_update_zip.py firmware.bin littlefs.bin update.zip
"""

from __future__ import annotations

import argparse
import os
import struct

MAGIC = b"K2UPD1\0\0"

def main() -> int:
    script_dir = os.path.dirname(os.path.abspath(__file__))

    ap = argparse.ArgumentParser()
    ap.add_argument("firmware", nargs="?", help="Firmware app binary (.bin)")
    ap.add_argument("littlefs", nargs="?", help="LittleFS image (.bin)")
    ap.add_argument("output", nargs="?", default="update.zip", help="Output file (default: update.zip)")
    args = ap.parse_args()

    fw_path = args.firmware or os.path.join(script_dir, "firmware.bin")
    fs_path = args.littlefs or os.path.join(script_dir, "littlefs.bin")

    if not os.path.isfile(fw_path):
        raise SystemExit(f"Firmware not found: {fw_path}")
    if not os.path.isfile(fs_path):
        raise SystemExit(f"LittleFS image not found: {fs_path}")

    with open(fw_path, "rb") as f:
        fw = f.read()
    with open(fs_path, "rb") as f:
        fs = f.read()

    if len(fw) == 0 or len(fs) == 0:
        raise SystemExit("Input files must not be empty")

    if len(fw) > 0xFFFFFFFF or len(fs) > 0xFFFFFFFF:
        raise SystemExit("Files too large for 32-bit sizes")

    header = MAGIC + struct.pack("<II", len(fw), len(fs))

    out_path = args.output
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(fw)
        f.write(fs)

    print(f"Wrote {out_path}")
    print(f"  firmware: {len(fw)} bytes")
    print(f"  littlefs: {len(fs)} bytes")
    print("Upload this file from the OTA page as update.zip")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
