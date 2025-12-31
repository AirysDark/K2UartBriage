# GitHub Copilot Instructions
# Project: ESP32 UART Rescue Bridge (Creality K2 / Allwinner R528)

## PROJECT PURPOSE
This firmware turns an ESP32 into a recovery, rescue, and service bridge for
Allwinner R528 / Creality K2 boards.

Primary goals:
- UART rescue when USB/FEL is unreliable or dead
- Safe recovery (no automatic flashing or erasing)
- U-Boot?aware backup and restore
- Strong debugging and observability

This is a RECOVERY TOOL, not a consumer product.
Reliability and debuggability are always higher priority than speed or size.

---

## ARCHITECTURE RULES (MANDATORY)

### 1. Modular design ONLY
Each subsystem must live in its own .h/.cpp pair.

Allowed modules include:
- backup_manager.*
- restore_manager.*
- uart_bridge.*
- web_ui.*
- storage.*
- pins.*
- debug_flags.*
- util.*

DO NOT merge unrelated logic into main.cpp.

---

### 2. main.cpp rules
main.cpp must ONLY:
- Initialize hardware
- Initialize modules
- Wire modules together
- Call loop()/tick() functions

main.cpp MUST NOT contain:
- Backup logic
- Restore logic
- Web UI HTML
- Protocol parsing
- JSON formatting logic

---

## DEBUG & LOGGING RULES

### Debug flags
All logging MUST be guarded by compile-time debug flags.

Example:
#if DEBUG_UART
DBG_PRINTF("[UART] baud=%lu\n", baud);
#endif

DO NOT use Serial.print() directly.

---

### Boot banners
Every module MUST print a boot banner on init.

Example:
printBootBanner("BACKUP", "U-Boot aware backup engine");

This is required for diagnosing partial UART failures.

---

## JSON RULES (ArduinoJson v7 ONLY)

REQUIRED:
- Use JsonDocument (NOT StaticJsonDocument)
- Use obj[key].to<JsonObject>()
- Use array.add<JsonObject>()

CORRECT:
JsonDocument d;
JsonObject wifi = d["wifi"].to<JsonObject>();

FORBIDDEN:
StaticJsonDocument<512> d;
d.createNestedObject("wifi");

---

## UART RULES

- All UART access must be centralized
- Baud autodetect must be non-blocking
- UART RX must fan out to:
  - USB Serial
  - TCP client
  - WebSocket clients

TX priority:
1. USB
2. TCP
3. WebSocket (explicitly enabled)

Never assume baud rate correctness.

---

## BACKUP RULES

- Backup MUST NOT start until U-Boot prompt is detected ('=> ')
- Backup output must be a SINGLE binary file
- Profiles:
  - A: Bootchain only
  - B: Boot + kernel
  - C: Early partitions
  - FULL: Entire device
  - CUSTOM: User-defined

CUSTOM ranges must be editable via Web UI.

---

## RESTORE RULES

- Restore file must be validated before use
- Restore must support:
  - Environment preview
  - Dry-run mode
- Restore MUST NOT auto-execute
- Restore requires explicit user action

---

## SAFETY RULES (CRITICAL)

Copilot MUST NEVER:
- Auto-flash
- Auto-erase
- Auto-write env
- Auto-enter FEL
- Assume boot mode

All destructive actions require:
- Explicit API call
- Clear UI button
- Human confirmation

---

## WIFI BEHAVIOR

Boot sequence:
1. Try STA using saved credentials
2. Timeout after 10 minutes
3. Fall back to AP + captive portal

AP mode MUST:
- Hijack DNS
- Redirect all traffic to /

Web UI MUST provide:
- Wi-Fi reset button
- Credential re-entry
- Reboot control

---

## GPIO RULES

- RESET and FEL pins must be explicitly controlled
- GPIO polarity must be documented
- Timing must be conservative

Example:
digitalWrite(PIN_TARGET_FEL, LOW);
delay(50);
targetResetPulse(200);
delay(600);
digitalWrite(PIN_TARGET_FEL, HIGH);

---

## COPILOT BEHAVIOR EXPECTATIONS

Copilot should:
- Prefer clarity over brevity
- Be defensive
- Assume unstable hardware
- Preserve debug hooks
- Add comments explaining WHY

If unsure:
- Ask for clarification
- Stub safely
- Leave TODO comments

---

## FORBIDDEN ACTIONS

Copilot MUST NOT:
- Introduce RTOS tasks without approval
- Add dynamic allocation in hot paths
- Replace working logic with ?simpler? logic
- Remove delays or safety checks
- Remove debug output

---

## FINAL RULE

This project exists to RECOVER BRICKED HARDWARE.
Every change must make recovery SAFER, not faster.

# Copilot instructions ? K2UartBriage (ESP32 UART Rescue Bridge)

You are working in an Arduino/PlatformIO ESP32 project that provides:
- UART bridge (USB serial + TCP raw + WebSocket console)
- Captive portal AP + STA credentials UI
- Target control GPIO: RESET + FEL/BOOT strap
- Backup/restore over UART with selectable profiles (A/B/C/FULL/CUSTOM)
- U-Boot prompt gating: do NOT start backup/restore until U-Boot prompt "=> " is detected

## Non-negotiables
1) Never start backup/restore until U-Boot is ready:
   - Wait for the prompt token "=> " (exact) before issuing any U-Boot commands.
2) Preserve existing module structure and separation:
   - `wifi_manager.*`, `web_ui.*`, `uart_bridge.*`, `backup_manager.*`, `restore_manager.*`
3) Every module prints a boot banner:
   - Use `printBootBanner("MOD", "short description")`
4) Debug flags:
   - Each module must be controllable via a `#define DEBUG_*` flag in `debug_flags.h`
   - Use DBG_PRINTF / D_* macros only (no raw Serial spam)
5) No blocking loops that freeze the web UI:
   - Any long task must run in a `tick()` state machine.
6) Keep RAM usage low:
   - Prefer streaming output; avoid giant buffers unless explicitly required.

## UART / Transport behavior
- UART target is `HardwareSerial(2)` configured in `pins.h`.
- TCP is single-client; reject additional clients with BUSY message.
- WebSocket console is best-effort text (OK for boot logs).
- USB serial always remains active.

## Backup profiles
- Profiles are LBA ranges (512-byte blocks).
- FULL is dangerous/slow; show warnings and require explicit user confirmation.
- Always validate custom ranges (max size limit, non-zero count).

## Web API conventions
- Routes live in `web_ui.*` (not in `main.cpp`).
- JSON uses ArduinoJson v7 style:
  - `JsonDocument doc;`
  - `doc["wifi"].to<JsonObject>()`
  - `arr.add<JsonObject>()`
- Responses must be small and predictable.

## Restore safety
- Must verify:
  - file header magic/version
  - checksum/CRC
  - board ID/SID when available
- If mismatch: refuse restore unless user explicitly forces.

## Preferred next steps
- Implement `.k2bak` container format:
  - header (magic/version/boardId)
  - one or more ranges (start/count + CRC)
  - payload data
- Add time estimation based on baud rate and size.