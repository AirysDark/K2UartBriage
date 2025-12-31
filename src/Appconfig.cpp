#include "AppConfig.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

// ============================================================
// Runtime constants (single definition point)
// ============================================================

// -------------------- Wi-Fi / Captive Portal runtime values --------------------
const char* CFG_WIFI_AP_SSID = CFG_WIFI_AP_SSID_LIT;
const char* CFG_WIFI_AP_PASS = CFG_WIFI_AP_PASS_LIT;

const uint16_t CFG_DNS_PORT = 53;
const IPAddress CFG_WIFI_AP_IP(192, 168, 0, 1);
const IPAddress CFG_WIFI_AP_NETMASK(255, 255, 255, 0);

const char* CFG_HOSTNAME = "k2-uart-bridge";

// -------------------- UART defaults --------------------
const uint32_t CFG_UART_DEFAULT_BAUD        = 115200;
const uint32_t CFG_UART_AUTODETECT_MIN_BAUD = 9600;
const uint32_t CFG_UART_AUTODETECT_MAX_BAUD = 2000000;

// -------------------- Command limits --------------------
const size_t CFG_CMD_LINEBUF_MAX  = 512;
const size_t CFG_CMD_LINEBUF_KEEP = 256;

// -------------------- Storage / FS paths --------------------
const char* CFG_PATH_BACKUPS_DIR = "/backups";
const char* CFG_PATH_FW_DIR      = "/fw";
const char* CFG_PATH_ENV_DIR     = "/env";

const char* CFG_PATH_BACKUP_FILE = "/backup.k2bak";
const char* CFG_PATH_FW_FILE     = "/firmware.bin";

const size_t CFG_IO_CHUNK_BYTES  = 4096;

// -------------------- SD Restore bundle (K2_restore on SD card) --------------------
const char* CFG_RESTORE_SD_DIR        = "/K2_restore";
const char* CFG_RESTORE_MANIFEST_PATH = "/K2_restore/manifest.json";
const char* CFG_RESTORE_PAYLOAD_DIR   = "/K2_restore/payload";

// -------------------- Backup container format caps --------------------
const uint32_t CFG_BACKUP_MAX_SECTIONS     = 64;
const size_t   CFG_BACKUP_SECTION_NAME_MAX = 64;

// Preferences namespace/keys used by backup manager
const char* CFG_PREF_NS_BACKUP   = "bridge";
const char* CFG_PREF_KEY_PROFILE = "bk_profile";
const char* CFG_PREF_KEY_CSTART  = "bk_cstart";
const char* CFG_PREF_KEY_CCOUNT  = "bk_ccount";

// -------------------- Web UI --------------------
const char* CFG_WEBUI_TITLE     = APP_NAME;
const char* CFG_WEBUI_BASE_PATH = "/";

// -------------------- U-Boot hex parser caps --------------------
const size_t CFG_UBOOT_HEX_MAX_LINE           = 256;
const size_t CFG_UBOOT_HEX_MAX_BYTES_PER_LINE = 128;

// -------------------- Blueprint runtime asset paths --------------------
const char* CFG_BP_DIR          = "/bp";
const char* CFG_BP_GCODE_JSON   = "/bp/gcode.json";
const char* CFG_BP_SCRIPTS_JSON = "/bp/scripts.json";
const char* CFG_BP_PROMPTS_JSON = "/bp/prompts.json";

// ============================================================
// Legacy aliases (keep older modules compiling)
// ============================================================

const char* AP_SSID = CFG_WIFI_AP_SSID;
const char* AP_PASS = CFG_WIFI_AP_PASS;

const uint16_t TCP_PORT  = (uint16_t)CFG_TCP_PORT;
const uint16_t HTTP_PORT = (uint16_t)CFG_WEB_PORT;
const uint16_t WS_PORT   = (uint16_t)CFG_WS_PORT;

const uint32_t WIFI_CONNECT_TIMEOUT_MS = (uint32_t)CFG_WIFI_CONNECT_TIMEOUT_MS;

const uint16_t DNS_PORT = CFG_DNS_PORT;
const IPAddress AP_IP = CFG_WIFI_AP_IP;
const IPAddress AP_NETMASK = CFG_WIFI_AP_NETMASK;

const char* HOSTNAME = CFG_HOSTNAME;

const uint32_t UART_DEFAULT_BAUD        = CFG_UART_DEFAULT_BAUD;
const uint32_t UART_AUTODETECT_MIN_BAUD = CFG_UART_AUTODETECT_MIN_BAUD;
const uint32_t UART_AUTODETECT_MAX_BAUD = CFG_UART_AUTODETECT_MAX_BAUD;

const size_t CMD_LINEBUF_MAX  = CFG_CMD_LINEBUF_MAX;
const size_t CMD_LINEBUF_KEEP = CFG_CMD_LINEBUF_KEEP;

const char* PATH_BACKUPS_DIR = CFG_PATH_BACKUPS_DIR;
const char* PATH_FW_DIR      = CFG_PATH_FW_DIR;
const char* PATH_ENV_DIR     = CFG_PATH_ENV_DIR;

const char* PATH_BACKUP_FILE = CFG_PATH_BACKUP_FILE;
const char* PATH_FW_FILE     = CFG_PATH_FW_FILE;

const size_t IO_CHUNK_BYTES  = CFG_IO_CHUNK_BYTES;

const uint32_t BACKUP_MAX_SECTIONS     = CFG_BACKUP_MAX_SECTIONS;
const size_t   BACKUP_SECTION_NAME_MAX = CFG_BACKUP_SECTION_NAME_MAX;

const char* WEBUI_TITLE     = CFG_WEBUI_TITLE;
const char* WEBUI_BASE_PATH = CFG_WEBUI_BASE_PATH;

const size_t UBOOT_HEX_MAX_LINE           = CFG_UBOOT_HEX_MAX_LINE;
const size_t UBOOT_HEX_MAX_BYTES_PER_LINE = CFG_UBOOT_HEX_MAX_BYTES_PER_LINE;

// ============================================================
// Boot banner helper
// ============================================================

// void printBootBanner(const char* module, const char* msg) {
//  if (!module) module = "?";
//  if (!msg) msg = "";
//  DBG_PRINTF("[%s] %s | %s v%s (%s)\n",
//             module, msg, APP_NAME, APP_VERSION, APP_BUILD);
//}