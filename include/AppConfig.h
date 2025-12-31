#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// ============================================================
// AppConfig.h
// - Compile-time config + extern runtime variables
// - NO PINS in this file
// - Runtime consts are defined in AppConfig.cpp
// ============================================================

// ============================================================
// 0) App Identity
// ============================================================
#ifndef APP_NAME
  #define APP_NAME "K2UartBriage"
#endif

#ifndef APP_VERSION
  #define APP_VERSION "2.0.0"
#endif

#ifndef APP_BUILD
  #define APP_BUILD __DATE__ " " __TIME__
#endif

#ifndef CFG_APP_NAME
  #define CFG_APP_NAME APP_NAME
#endif
#ifndef CFG_APP_VERSION
  #define CFG_APP_VERSION APP_VERSION
#endif
#ifndef CFG_APP_BUILD
  #define CFG_APP_BUILD APP_BUILD
#endif

// ============================================================
// 1) Debug Toggles (prefer platformio.ini -DDEBUG_*=1)
// ============================================================
#ifndef DEBUG_MAIN
  #define DEBUG_MAIN 1
#endif
#ifndef DEBUG_WIFI
  #define DEBUG_WIFI 1
#endif
#ifndef DEBUG_UART
  #define DEBUG_UART 1
#endif
#ifndef DEBUG_WEB
  #define DEBUG_WEB 1
#endif
#ifndef DEBUG_TCP
  #define DEBUG_TCP 1
#endif
#ifndef DEBUG_STORAGE
  #define DEBUG_STORAGE 1
#endif
#ifndef DEBUG_BACKUP
  #define DEBUG_BACKUP 1
#endif
#ifndef DEBUG_OTA
  #define DEBUG_OTA 1
#endif
#ifndef DEBUG_RESTORE
  #define DEBUG_RESTORE 1
#endif
#ifndef DEBUG_ENV
  #define DEBUG_ENV 1
#endif

#ifndef CFG_DEBUG_MAIN
  #define CFG_DEBUG_MAIN DEBUG_MAIN
#endif
#ifndef CFG_DEBUG_WIFI
  #define CFG_DEBUG_WIFI DEBUG_WIFI
#endif
#ifndef CFG_DEBUG_UART
  #define CFG_DEBUG_UART DEBUG_UART
#endif
#ifndef CFG_DEBUG_WEB
  #define CFG_DEBUG_WEB DEBUG_WEB
#endif
#ifndef CFG_DEBUG_TCP
  #define CFG_DEBUG_TCP DEBUG_TCP
#endif
#ifndef CFG_DEBUG_STORAGE
  #define CFG_DEBUG_STORAGE DEBUG_STORAGE
#endif
#ifndef CFG_DEBUG_BACKUP
  #define CFG_DEBUG_BACKUP DEBUG_BACKUP
#endif
#ifndef CFG_DEBUG_OTA
  #define CFG_DEBUG_OTA DEBUG_OTA
#endif
#ifndef CFG_DEBUG_RESTORE
  #define CFG_DEBUG_RESTORE DEBUG_RESTORE
#endif
#ifndef CFG_DEBUG_ENV
  #define CFG_DEBUG_ENV DEBUG_ENV
#endif

// ============================================================
// 2) Feature Toggles
// ============================================================
#ifndef ENABLE_CAPTIVE_PORTAL
  #define ENABLE_CAPTIVE_PORTAL 1
#endif
#ifndef ENABLE_OTA
  #define ENABLE_OTA 1
#endif

#ifndef CFG_ENABLE_CAPTIVE_PORTAL
  #define CFG_ENABLE_CAPTIVE_PORTAL ENABLE_CAPTIVE_PORTAL
#endif
#ifndef CFG_ENABLE_OTA
  #define CFG_ENABLE_OTA ENABLE_OTA
#endif

// ============================================================
// 3) Wi-Fi / Captive Portal Defaults (compile-time literals)
// ============================================================
// NOTE: Some PROGMEM pages prefer compile-time literals.
#ifndef AP_SSID_LIT
  #define AP_SSID_LIT "ESP32-UART-DRIAGE"
#endif
#ifndef AP_PASS_LIT
  #define AP_PASS_LIT "12345678"
#endif

#ifndef CFG_WIFI_AP_SSID_LIT
  #define CFG_WIFI_AP_SSID_LIT AP_SSID_LIT
#endif
#ifndef CFG_WIFI_AP_PASS_LIT
  #define CFG_WIFI_AP_PASS_LIT AP_PASS_LIT
#endif

#ifndef CFG_WIFI_CONNECT_TIMEOUT_MS
  #define CFG_WIFI_CONNECT_TIMEOUT_MS (10UL * 60UL * 1000UL) // 10 minutes
#endif

// Runtime vars (defined in AppConfig.cpp)
extern const char*   CFG_WIFI_AP_SSID;
extern const char*   CFG_WIFI_AP_PASS;

extern const uint16_t CFG_DNS_PORT;
extern const IPAddress CFG_WIFI_AP_IP;
extern const IPAddress CFG_WIFI_AP_NETMASK;

extern const char*   CFG_HOSTNAME;

// ============================================================
// 4) Ports
// ============================================================
#ifndef CFG_TCP_PORT
  #define CFG_TCP_PORT 3333
#endif
#ifndef CFG_WEB_PORT
  #define CFG_WEB_PORT 80
#endif
#ifndef CFG_WS_PORT
  #define CFG_WS_PORT 80
#endif

// ============================================================
// 5) TCP UART Server Behavior
// ============================================================
#ifndef TCPUART_SINGLE_CLIENT_ONLY
  #define TCPUART_SINGLE_CLIENT_ONLY 1
#endif
#ifndef TCPUART_NO_DELAY
  #define TCPUART_NO_DELAY 1
#endif
#ifndef TCPUART_SEND_GREETING
  #define TCPUART_SEND_GREETING 1
#endif
#ifndef TCPUART_GREETING_LIT
  #define TCPUART_GREETING_LIT "K2UartBriage TCP bridge ready.\n"
#endif
#ifndef TCPUART_CLIENT_RXBUF
  #define TCPUART_CLIENT_RXBUF 2048
#endif
#ifndef TCPUART_CLIENT_TXBUF
  #define TCPUART_CLIENT_TXBUF 2048
#endif

#ifndef CFG_TCPUART_SINGLE_CLIENT_ONLY
  #define CFG_TCPUART_SINGLE_CLIENT_ONLY TCPUART_SINGLE_CLIENT_ONLY
#endif
#ifndef CFG_TCPUART_NO_DELAY
  #define CFG_TCPUART_NO_DELAY TCPUART_NO_DELAY
#endif
#ifndef CFG_TCPUART_SEND_GREETING
  #define CFG_TCPUART_SEND_GREETING TCPUART_SEND_GREETING
#endif
#ifndef CFG_TCPUART_GREETING_LIT
  #define CFG_TCPUART_GREETING_LIT TCPUART_GREETING_LIT
#endif
#ifndef CFG_TCPUART_CLIENT_RXBUF
  #define CFG_TCPUART_CLIENT_RXBUF TCPUART_CLIENT_RXBUF
#endif
#ifndef CFG_TCPUART_CLIENT_TXBUF
  #define CFG_TCPUART_CLIENT_TXBUF TCPUART_CLIENT_TXBUF
#endif

// ============================================================
// 6) UART Defaults
// ============================================================
extern const uint32_t CFG_UART_DEFAULT_BAUD;
extern const uint32_t CFG_UART_AUTODETECT_MIN_BAUD;
extern const uint32_t CFG_UART_AUTODETECT_MAX_BAUD;

#ifndef UART_AUTODETECT_SAMPLE_MS
  #define UART_AUTODETECT_SAMPLE_MS 700
#endif
#ifndef UART_AUTODETECT_MIN_BYTES
  #define UART_AUTODETECT_MIN_BYTES 16
#endif
#ifndef CFG_UART_AUTODETECT_SAMPLE_MS
  #define CFG_UART_AUTODETECT_SAMPLE_MS UART_AUTODETECT_SAMPLE_MS
#endif
#ifndef CFG_UART_AUTODETECT_MIN_BYTES
  #define CFG_UART_AUTODETECT_MIN_BYTES UART_AUTODETECT_MIN_BYTES
#endif

// ============================================================
// 7) Command / Console Limits
// ============================================================
extern const size_t CFG_CMD_LINEBUF_MAX;
extern const size_t CFG_CMD_LINEBUF_KEEP;

// ============================================================
// 8) Storage / FS Paths + IO
// ============================================================
extern const char*  CFG_PATH_BACKUPS_DIR;
extern const char*  CFG_PATH_FW_DIR;
extern const char*  CFG_PATH_ENV_DIR;

extern const char*  CFG_PATH_BACKUP_FILE;
extern const char*  CFG_PATH_FW_FILE;

extern const size_t CFG_IO_CHUNK_BYTES;

#ifndef CFG_SD_SPI_HZ
  #define CFG_SD_SPI_HZ 16000000UL
#endif

// ============================================================
// 8B) SD Restore Bundle (K2_restore on SD card)
// ============================================================
extern const char* CFG_RESTORE_SD_DIR;
extern const char* CFG_RESTORE_MANIFEST_PATH;
extern const char* CFG_RESTORE_PAYLOAD_DIR;

// ============================================================
// 9) Backup / Restore Limits
// ============================================================
#ifndef CFG_BACKUP_PROFILE_A_LBA_START
  #define CFG_BACKUP_PROFILE_A_LBA_START 0x0000UL
#endif
#ifndef CFG_BACKUP_PROFILE_A_LBA_COUNT
  #define CFG_BACKUP_PROFILE_A_LBA_COUNT 0x4000UL
#endif
#ifndef CFG_BACKUP_PROFILE_B_LBA_START
  #define CFG_BACKUP_PROFILE_B_LBA_START 0x0000UL
#endif
#ifndef CFG_BACKUP_PROFILE_B_LBA_COUNT
  #define CFG_BACKUP_PROFILE_B_LBA_COUNT 0x20000UL
#endif
#ifndef CFG_BACKUP_PROFILE_C_LBA_START
  #define CFG_BACKUP_PROFILE_C_LBA_START 0x0000UL
#endif
#ifndef CFG_BACKUP_PROFILE_C_LBA_COUNT
  #define CFG_BACKUP_PROFILE_C_LBA_COUNT 0x80000UL
#endif
#ifndef CFG_BACKUP_PROFILE_FULL_LBA_START
  #define CFG_BACKUP_PROFILE_FULL_LBA_START 0x0000UL
#endif
#ifndef CFG_BACKUP_PROFILE_FULL_LBA_COUNT
  #define CFG_BACKUP_PROFILE_FULL_LBA_COUNT 0x400000UL
#endif

#ifndef CFG_K2BAK_VERSION_V1
  #define CFG_K2BAK_VERSION_V1 1
#endif
#ifndef CFG_K2BAK_VERSION_V2
  #define CFG_K2BAK_VERSION_V2 2
#endif

extern const uint32_t CFG_BACKUP_MAX_SECTIONS;
extern const size_t   CFG_BACKUP_SECTION_NAME_MAX;

#ifndef CFG_BACKUP_MAX_BYTES
  #define CFG_BACKUP_MAX_BYTES (8UL * 1024UL * 1024UL)
#endif
#ifndef CFG_BACKUP_DEFAULT_BLOCKS_PER_CHUNK
  #define CFG_BACKUP_DEFAULT_BLOCKS_PER_CHUNK 64UL
#endif

extern const char* CFG_PREF_NS_BACKUP;
extern const char* CFG_PREF_KEY_PROFILE;
extern const char* CFG_PREF_KEY_CSTART;
extern const char* CFG_PREF_KEY_CCOUNT;

// ============================================================
// 10) Web UI / Pages
// ============================================================
extern const char* CFG_WEBUI_TITLE;
extern const char* CFG_WEBUI_BASE_PATH;

// ============================================================
// 11) U-Boot Hex Parser Caps
// ============================================================
extern const size_t CFG_UBOOT_HEX_MAX_LINE;
extern const size_t CFG_UBOOT_HEX_MAX_BYTES_PER_LINE;

// ============================================================
// 12) Ring Buffer / Logging
// ============================================================
#ifndef CFG_LOGBUF_SIZE
  #define CFG_LOGBUF_SIZE (16UL * 1024UL)
#endif

// ============================================================
// 13) SafeGuard Policy
// ============================================================

// unsafe window duration
#ifndef CFG_SG_UNSAFE_TIMEOUT_MS
  #define CFG_SG_UNSAFE_TIMEOUT_MS (5UL * 60UL * 1000UL) // 5 minutes
#endif

// ---- Informational (SAFE) ----
#ifndef CFG_SG_BLOCK_HELP
  #define CFG_SG_BLOCK_HELP 0
#endif
#ifndef CFG_SG_BLOCK_STATUS
  #define CFG_SG_BLOCK_STATUS 0
#endif
#ifndef CFG_SG_BLOCK_WIFI_STATUS
  #define CFG_SG_BLOCK_WIFI_STATUS 0
#endif
#ifndef CFG_SG_BLOCK_TCP_STATUS
  #define CFG_SG_BLOCK_TCP_STATUS 0
#endif
#ifndef CFG_SG_BLOCK_OTA_STATUS
  #define CFG_SG_BLOCK_OTA_STATUS 0
#endif
#ifndef CFG_SG_BLOCK_SD_STATUS
  #define CFG_SG_BLOCK_SD_STATUS 0
#endif

// ---- UART control ----
#ifndef CFG_SG_BLOCK_UART_SET
  #define CFG_SG_BLOCK_UART_SET 0
#endif
#ifndef CFG_SG_BLOCK_UART_AUTO
  #define CFG_SG_BLOCK_UART_AUTO 0
#endif
#ifndef CFG_SG_BLOCK_UART_DETECT
  #define CFG_SG_BLOCK_UART_DETECT 0
#endif

// ---- Target control (DANGEROUS) ----
#ifndef CFG_SG_BLOCK_TARGET_RESET
  #define CFG_SG_BLOCK_TARGET_RESET 1
#endif
#ifndef CFG_SG_BLOCK_TARGET_FEL
  #define CFG_SG_BLOCK_TARGET_FEL 1
#endif

// ---- Env capture ----
#ifndef CFG_SG_BLOCK_ENV_CAPTURE
  #define CFG_SG_BLOCK_ENV_CAPTURE 0
#endif
#ifndef CFG_SG_BLOCK_ENV_SHOW
  #define CFG_SG_BLOCK_ENV_SHOW 0
#endif
#ifndef CFG_SG_BLOCK_ENV_BOARDID
  #define CFG_SG_BLOCK_ENV_BOARDID 0
#endif
#ifndef CFG_SG_BLOCK_ENV_LAYOUT
  #define CFG_SG_BLOCK_ENV_LAYOUT 0
#endif

// ---- Backup ----
#ifndef CFG_SG_BLOCK_BACKUP_START_UART
  #define CFG_SG_BLOCK_BACKUP_START_UART 1
#endif
#ifndef CFG_SG_BLOCK_BACKUP_START_META
  #define CFG_SG_BLOCK_BACKUP_START_META 1
#endif
#ifndef CFG_SG_BLOCK_BACKUP_STATUS
  #define CFG_SG_BLOCK_BACKUP_STATUS 0
#endif
#ifndef CFG_SG_BLOCK_BACKUP_PROFILE
  #define CFG_SG_BLOCK_BACKUP_PROFILE 0
#endif
#ifndef CFG_SG_BLOCK_BACKUP_CUSTOM
  #define CFG_SG_BLOCK_BACKUP_CUSTOM 1
#endif

// ---- Restore ----
#ifndef CFG_SG_BLOCK_RESTORE_PLAN
  #define CFG_SG_BLOCK_RESTORE_PLAN 0
#endif
#ifndef CFG_SG_BLOCK_RESTORE_ARM
  #define CFG_SG_BLOCK_RESTORE_ARM 1
#endif
#ifndef CFG_SG_BLOCK_RESTORE_DISARM
  #define CFG_SG_BLOCK_RESTORE_DISARM 0
#endif
#ifndef CFG_SG_BLOCK_RESTORE_APPLY
  #define CFG_SG_BLOCK_RESTORE_APPLY 1
#endif
#ifndef CFG_SG_BLOCK_RESTORE_VERIFY
  #define CFG_SG_BLOCK_RESTORE_VERIFY 1
#endif

// ---- SD deletes ----
#ifndef CFG_SG_BLOCK_SD_RM
  #define CFG_SG_BLOCK_SD_RM 1
#endif

// ============================================================
// 14) Blueprint Runtime (assets on LittleFS)
// ============================================================
#ifndef CFG_BP_ENABLE
  #define CFG_BP_ENABLE 1
#endif

extern const char* CFG_BP_DIR;
extern const char* CFG_BP_GCODE_JSON;
extern const char* CFG_BP_SCRIPTS_JSON;
extern const char* CFG_BP_PROMPTS_JSON;

// Compatibility aliases (BlueprintRuntime.cpp may still use *_PATH)
#ifndef CFG_BP_GCODE_PATH
  #define CFG_BP_GCODE_PATH CFG_BP_GCODE_JSON
#endif
#ifndef CFG_BP_SCRIPTS_PATH
  #define CFG_BP_SCRIPTS_PATH CFG_BP_SCRIPTS_JSON
#endif
#ifndef CFG_BP_PROMPTS_PATH
  #define CFG_BP_PROMPTS_PATH CFG_BP_PROMPTS_JSON
#endif

#ifndef CFG_BP_MAX_LINE
  #define CFG_BP_MAX_LINE 256
#endif
#ifndef CFG_BP_SCRIPT_STEP_DELAY_MS
  #define CFG_BP_SCRIPT_STEP_DELAY_MS 80
#endif
#ifndef CFG_BP_SCRIPT_TIMEOUT_MS
  #define CFG_BP_SCRIPT_TIMEOUT_MS 4000UL
#endif

// ============================================================
// NEW: Hidden WS (admin channel) + CK2 (special code)
// ============================================================

// ---- Hidden WS / admin channel (K2BUI) ----
#ifndef CFG_HIDDEN_WS_ENABLE
  #define CFG_HIDDEN_WS_ENABLE 1
#endif

#ifndef CFG_HIDDEN_WS_PATH
  #define CFG_HIDDEN_WS_PATH "/_sys/ws"
#endif

#ifndef CFG_HIDDEN_WS_MAX_CLIENTS
  #define CFG_HIDDEN_WS_MAX_CLIENTS 1
#endif

#ifndef CFG_HIDDEN_WS_TOKEN_DEFAULT
  #define CFG_HIDDEN_WS_TOKEN_DEFAULT "CHANGE_ME_TOKEN"
#endif

#ifndef CFG_HIDDEN_WS_ALLOW_UART_BINARY
  #define CFG_HIDDEN_WS_ALLOW_UART_BINARY 1
#endif

#ifndef CFG_HIDDEN_WS_RATE_LIMIT_MS
  #define CFG_HIDDEN_WS_RATE_LIMIT_MS 25
#endif

#ifndef CFG_PREFS_NS
  #define CFG_PREFS_NS "bridge"
#endif

#ifndef CFG_PREFS_WS_TOKEN_KEY
  #define CFG_PREFS_WS_TOKEN_KEY "wsToken"
#endif

// ---- CK2 (your special generated code) ----
// Stored in FS + downloadable from Web UI. Used by your external UI to auth WS.
#ifndef CFG_CK2_ENABLE
  #define CFG_CK2_ENABLE 1
#endif

#ifndef CFG_CK2_FILE_PATH
  // LittleFS path where the generated code is stored
  #define CFG_CK2_FILE_PATH "/ck2.key"
#endif

#ifndef CFG_CK2_DOWNLOAD_NAME
  // filename the browser downloads
  #define CFG_CK2_DOWNLOAD_NAME "CK2.key"
#endif

#ifndef CFG_CK2_CODE_BYTES
  // how many random bytes we generate (before encoding)
  #define CFG_CK2_CODE_BYTES 32
#endif

#ifndef CFG_CK2_MAX_AGE_DAYS
  // YOU ASKED: NOT 7 days / not expiring.
  // 0 = never expire (infinite)
  #define CFG_CK2_MAX_AGE_DAYS 0
#endif

// ============================================================
// Legacy aliases (keep old code compiling; remove later)
// ============================================================
extern const char* AP_SSID;
extern const char* AP_PASS;

extern const uint16_t TCP_PORT;
extern const uint16_t HTTP_PORT;
extern const uint16_t WS_PORT;

extern const uint32_t WIFI_CONNECT_TIMEOUT_MS;

extern const uint16_t DNS_PORT;
extern const IPAddress AP_IP;
extern const IPAddress AP_NETMASK;

extern const char* HOSTNAME;

extern const uint32_t UART_DEFAULT_BAUD;
extern const uint32_t UART_AUTODETECT_MIN_BAUD;
extern const uint32_t UART_AUTODETECT_MAX_BAUD;

extern const size_t CMD_LINEBUF_MAX;
extern const size_t CMD_LINEBUF_KEEP;

extern const char* PATH_BACKUPS_DIR;
extern const char* PATH_FW_DIR;
extern const char* PATH_ENV_DIR;

extern const char* PATH_BACKUP_FILE;
extern const char* PATH_FW_FILE;

extern const size_t IO_CHUNK_BYTES;

extern const uint32_t BACKUP_MAX_SECTIONS;
extern const size_t   BACKUP_SECTION_NAME_MAX;

extern const char* WEBUI_TITLE;
extern const char* WEBUI_BASE_PATH;

extern const size_t UBOOT_HEX_MAX_LINE;
extern const size_t UBOOT_HEX_MAX_BYTES_PER_LINE;

// Boot banner helper (used by several modules)
void printBootBanner(const char* module, const char* msg);