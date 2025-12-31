# ESP32 UART Rescue Bridge (v2) — Backup/Restore Selection (A/B/C/FULL)

This firmware provides:
- USB Serial <-> Target UART bridge
- TCP UART bridge (PuTTY RAW/Telnet)
- Captive portal AP + STA mode with fallback to AP after 10 minutes
- GPIO Target RESET + FEL/BOOT strap control
- Baud autodetect
- Web UI
- Backup/Restore framework with selectable modes A/B/C/FULL (+ Custom)

## IMPORTANT (UART backup reality)
U-Boot generally cannot "send" binary over UART efficiently.
The MVP backup implemented here captures:
- U-Boot environment (printenv output)
- Selected profile metadata

Next step (we can add after you confirm layout):
- Block dump via `mmc read` + `md.b` and hex-parse into binary (slow)
- Restore via `loadx`/`loady` and `mmc write` (requires exact partitions/LBA map)

## Build / Flash
PlatformIO:
- Open folder in VSCode + PlatformIO
- Build/Upload (env: esp32dev)

## Wiring
ESP32 RX2(GPIO16)  <- Target TX
ESP32 TX2(GPIO17)  -> Target RX
GND shared.

RESET/FEL pins MUST be level-safe (transistor or open-drain style).
