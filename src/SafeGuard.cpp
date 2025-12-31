#include "SafeGuard.h"
#include "AppConfig.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static bool     g_unsafe = false;
static uint32_t g_unsafeUntil = 0;

void SafeGuard::begin() {
  g_unsafe = false;
  g_unsafeUntil = 0;
}

void SafeGuard::tick() {
  if (!g_unsafe) return;
  if (g_unsafeUntil && (int32_t)(millis() - g_unsafeUntil) >= 0) {
    g_unsafe = false;
    g_unsafeUntil = 0;
  }
}

void SafeGuard::setUnsafe(bool on) {
  g_unsafe = on;
  if (on) g_unsafeUntil = millis() + (uint32_t)CFG_SG_UNSAFE_TIMEOUT_MS;
  else    g_unsafeUntil = 0;
}

bool SafeGuard::isUnsafe() {
  return g_unsafe;
}

uint32_t SafeGuard::unsafeRemainingMs() {
  if (!g_unsafe || !g_unsafeUntil) return 0;
  int32_t rem = (int32_t)(g_unsafeUntil - millis());
  return rem > 0 ? (uint32_t)rem : 0;
}

// Helper: macro gate
static bool blockedBy(int blockMacro, const char* name, String* why) {
  if (!blockMacro) return false;
  if (why) *why = String("SafeGuard blocked: ") + name + " (use !unsafe on)";
  return true;
}

bool SafeGuard::allow(const String& head, const String& sub, const String& arg, String* whyBlocked) {
  // If unsafe armed, everything passes
  if (g_unsafe) return true;

  // ---- help/status buckets ----
  if (head.equalsIgnoreCase("help"))   return !blockedBy(CFG_SG_BLOCK_HELP, "help", whyBlocked);
  if (head.equalsIgnoreCase("status")) return !blockedBy(CFG_SG_BLOCK_STATUS, "status", whyBlocked);

  if (head.equalsIgnoreCase("wifi") && sub.equalsIgnoreCase("status"))
    return !blockedBy(CFG_SG_BLOCK_WIFI_STATUS, "wifi status", whyBlocked);

  if (head.equalsIgnoreCase("tcp") && sub.equalsIgnoreCase("status"))
    return !blockedBy(CFG_SG_BLOCK_TCP_STATUS, "tcp status", whyBlocked);

  if (head.equalsIgnoreCase("ota") && sub.equalsIgnoreCase("status"))
    return !blockedBy(CFG_SG_BLOCK_OTA_STATUS, "ota status", whyBlocked);

  if (head.equalsIgnoreCase("sd") && sub.equalsIgnoreCase("status"))
    return !blockedBy(CFG_SG_BLOCK_SD_STATUS, "sd status", whyBlocked);

  // ---- uart ----
  if (head.equalsIgnoreCase("uart") && sub.equalsIgnoreCase("set"))
    return !blockedBy(CFG_SG_BLOCK_UART_SET, "uart set", whyBlocked);

  if (head.equalsIgnoreCase("uart") && sub.equalsIgnoreCase("auto"))
    return !blockedBy(CFG_SG_BLOCK_UART_AUTO, "uart auto", whyBlocked);

  if (head.equalsIgnoreCase("uart") && sub.equalsIgnoreCase("detect"))
    return !blockedBy(CFG_SG_BLOCK_UART_DETECT, "uart detect", whyBlocked);

  // ---- target ----
  if (head.equalsIgnoreCase("target") && sub.equalsIgnoreCase("reset"))
    return !blockedBy(CFG_SG_BLOCK_TARGET_RESET, "target reset", whyBlocked);

  if (head.equalsIgnoreCase("target") && sub.equalsIgnoreCase("fel"))
    return !blockedBy(CFG_SG_BLOCK_TARGET_FEL, "target fel", whyBlocked);

  // ---- env ----
  if (head.equalsIgnoreCase("env") && sub.equalsIgnoreCase("capture"))
    return !blockedBy(CFG_SG_BLOCK_ENV_CAPTURE, "env capture", whyBlocked);

  if (head.equalsIgnoreCase("env") && sub.equalsIgnoreCase("show"))
    return !blockedBy(CFG_SG_BLOCK_ENV_SHOW, "env show", whyBlocked);

  if (head.equalsIgnoreCase("env") && sub.equalsIgnoreCase("boardid"))
    return !blockedBy(CFG_SG_BLOCK_ENV_BOARDID, "env boardid", whyBlocked);

  if (head.equalsIgnoreCase("env") && sub.equalsIgnoreCase("layout"))
    return !blockedBy(CFG_SG_BLOCK_ENV_LAYOUT, "env layout", whyBlocked);

  // ---- backup ----
  if (head.equalsIgnoreCase("backup") && sub.equalsIgnoreCase("start")) {
    if (arg.equalsIgnoreCase("uart")) return !blockedBy(CFG_SG_BLOCK_BACKUP_START_UART, "backup start uart", whyBlocked);
    if (arg.equalsIgnoreCase("meta")) return !blockedBy(CFG_SG_BLOCK_BACKUP_START_META, "backup start meta", whyBlocked);
    return true;
  }

  if (head.equalsIgnoreCase("backup") && sub.equalsIgnoreCase("status"))
    return !blockedBy(CFG_SG_BLOCK_BACKUP_STATUS, "backup status", whyBlocked);

  if (head.equalsIgnoreCase("backup") && sub.equalsIgnoreCase("profile"))
    return !blockedBy(CFG_SG_BLOCK_BACKUP_PROFILE, "backup profile", whyBlocked);

  if (head.equalsIgnoreCase("backup") && sub.equalsIgnoreCase("custom"))
    return !blockedBy(CFG_SG_BLOCK_BACKUP_CUSTOM, "backup custom", whyBlocked);

  // ---- restore ----
  if (head.equalsIgnoreCase("restore") && sub.equalsIgnoreCase("plan"))
    return !blockedBy(CFG_SG_BLOCK_RESTORE_PLAN, "restore plan", whyBlocked);

  if (head.equalsIgnoreCase("restore") && sub.equalsIgnoreCase("arm"))
    return !blockedBy(CFG_SG_BLOCK_RESTORE_ARM, "restore arm", whyBlocked);

  if (head.equalsIgnoreCase("restore") && sub.equalsIgnoreCase("disarm"))
    return !blockedBy(CFG_SG_BLOCK_RESTORE_DISARM, "restore disarm", whyBlocked);

  if (head.equalsIgnoreCase("restore") && sub.equalsIgnoreCase("apply"))
    return !blockedBy(CFG_SG_BLOCK_RESTORE_APPLY, "restore apply", whyBlocked);

  if (head.equalsIgnoreCase("restore") && sub.equalsIgnoreCase("verify"))
    return !blockedBy(CFG_SG_BLOCK_RESTORE_VERIFY, "restore verify", whyBlocked);

  // ---- sd rm ----
  if (head.equalsIgnoreCase("sd") && sub.equalsIgnoreCase("rm"))
    return !blockedBy(CFG_SG_BLOCK_SD_RM, "sd rm", whyBlocked);

  // Default allow if it?s unknown (so you don?t brick development)
  return true;
}