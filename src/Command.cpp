#include "Command.h"
#include "Debug.h"
#include "SafeGuard.h"
#include "BlueprintRuntime.h"
#include "AppConfig.h"

#include <type_traits>
#include <utility>

DBG_REGISTER_MODULE(__FILE__);

static Command::Context* gCtx = nullptr;

// Per-source line buffers (only used for line-based console parsing)
static String gBufUsb;
static String gBufWs;
static String gBufTcp;

// ============================================================
// BlueprintRuntime API adapter
// ============================================================
static inline const String& bp_lastLine_ref() { return BlueprintRuntime::lastLine(); }
static inline String bp_getKey_str(const String& k) { return BlueprintRuntime::getKey(k); }

// ============================================================

bool Command::isHexString(const String& s) {
  if (s.length() == 0) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

void Command::begin(Context* ctx) {
  gCtx = ctx;
  gBufUsb.reserve(256);
  gBufWs.reserve(256);
  gBufTcp.reserve(256);
}

String& Command::buf(Source src) {
  switch (src) {
    case Source::USB: return gBufUsb;
    case Source::WS:  return gBufWs;
    case Source::TCP: return gBufTcp;
    default:          return gBufUsb;
  }
}

bool Command::feedText(Source src, const char* s) {
  if (!s) return false;
  return feed(src, (const uint8_t*)s, strlen(s));
}

bool Command::feed(Source src, const uint8_t* data, size_t len) {
  if (!gCtx || !data || !len) return false;

  String& b = buf(src);
  bool consumedAny = false;

  // If targetWrite isn't wired, passthrough can't work.
  const bool canPassthrough = (gCtx->targetWrite != nullptr) || (gCtx->targetWriteLine != nullptr);

  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];

    // Ignore CR (we normalize on LF)
    if (c == '\r') continue;

    // ---- newline completes a line ----
    if (c == '\n') {
      String line = b;
      b = "";

      line.trim();

      // Blank line still gets forwarded to target (pokes prompt)
      if (line.length() == 0) {
        if (canPassthrough && gCtx->targetWrite) {
          const char nl = '\n';
          gCtx->targetWrite((const uint8_t*)&nl, 1);
        } else if (canPassthrough && gCtx->targetWriteLine) {
          gCtx->targetWriteLine("");
        }
        continue;
      }

      if (startsWithBang(line)) {
        handleLine(src, line);
        consumedAny = true;
      } else {
        // PASS THROUGH TO TARGET
        if (gCtx->targetWriteLine) {
          gCtx->targetWriteLine(line);
        } else if (gCtx->targetWrite) {
          gCtx->targetWrite((const uint8_t*)line.c_str(), line.length());
          const char nl = '\n';
          gCtx->targetWrite((const uint8_t*)&nl, 1);
        }
      }
      continue;
    }

    // ---- raw passthrough (binary-safe) until newline ----
    if (b.length() == 0 && c != '!') {
      if (canPassthrough && gCtx->targetWrite) {
        gCtx->targetWrite((const uint8_t*)&c, 1);
      } else {
        b += c;
      }
      continue;
    }

    // Otherwise we are building a line (either command, or typed text)
    b += c;

    // prevent runaway memory if someone pastes junk
    if (b.length() > 512) {
      b.remove(0, b.length() - 256);
    }
  }

  return consumedAny;
}

bool Command::startsWithBang(const String& s) {
  return s.length() > 0 && s[0] == '!';
}

void Command::say(Source src, const String& s) {
  if (!gCtx) return;
  if (gCtx->reply) gCtx->reply(src, s.c_str());
}

void Command::sayLn(Source src, const String& s) {
  if (!gCtx) return;
  if (gCtx->replyLn) gCtx->replyLn(src, s.c_str());
  else if (gCtx->reply) {
    String t = s; t += "\n";
    gCtx->reply(src, t.c_str());
  }
}

bool Command::parseU32(const String& s, uint32_t& out) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;

  if (t.startsWith("0x") || t.startsWith("0X")) {
    out = (uint32_t)strtoul(t.c_str(), nullptr, 16);
    return true;
  }

  bool hasHexAlpha = false;
  for (size_t i = 0; i < t.length(); i++) {
    char c = t[i];
    if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) { hasHexAlpha = true; break; }
  }
  if (hasHexAlpha && isHexString(t)) {
    out = (uint32_t)strtoul(t.c_str(), nullptr, 16);
    return true;
  }

  char* endp = nullptr;
  unsigned long v = strtoul(t.c_str(), &endp, 10);
  if (endp == t.c_str()) return false;
  out = (uint32_t)v;
  return true;
}

bool Command::parseBoolOnOff(const String& s, bool& out) {
  String t = s; t.trim();
  if (t.equalsIgnoreCase("on") || t.equalsIgnoreCase("1") || t.equalsIgnoreCase("true") || t.equalsIgnoreCase("enable")) { out = true; return true; }
  if (t.equalsIgnoreCase("off")|| t.equalsIgnoreCase("0") || t.equalsIgnoreCase("false")|| t.equalsIgnoreCase("disable")) { out = false; return true; }
  return false;
}

void Command::splitFirst(const String& in, String& head, String& tail) {
  int sp = in.indexOf(' ');
  head = (sp >= 0) ? in.substring(0, sp) : in;
  tail = (sp >= 0) ? in.substring(sp + 1) : "";
  head.trim(); tail.trim();
}

void Command::splitTwo(const String& in, String& a, String& b2) {
  String t = in; t.trim();
  int sp = t.indexOf(' ');
  a = (sp >= 0) ? t.substring(0, sp) : t;
  b2 = (sp >= 0) ? t.substring(sp + 1) : "";
  a.trim(); b2.trim();
}

void Command::showHelp(Source src) {
  sayLn(src,
    "Commands:\n"
    "  !help\n"
    "  !status\n"
    "  !reboot\n"
    "\n"
    "  !target reset [ms]\n"
    "  !target fel\n"
    "\n"
    "  !wifi status\n"
    "  !wifi save <ssid> <pass>\n"
    "  !wifi reset\n"
    "\n"
    "  !ap start\n"
    "  !sta start\n"
    "  !ap timer show\n"
    "  !ap timer set <ms>\n"
    "  !ap timer enable\n"
    "  !ap timer disable\n"
    "\n"
    "  !uart status\n"
    "  !uart baud <rate>\n"
    "  !uart auto on|off\n"
    "  !uart autodetect\n"
    "\n"
    "  !uboot prompt\n"
    "  !ums start\n"
    "  !ums clear\n"
    "\n"
    "  !env capture\n"
    "  !env show\n"
    "  !env boardid\n"
    "  !env layout\n"
    "\n"
    "  !bp status\n"
    "  !bp keys\n"
    "  !bp get <key>\n"
    "  !bp scripts\n"
    "  !bp run <name> [timeoutMs]\n"
    "  !bp prompts\n"
    "  !bp prompt <name>\n"
    "  !bp gcode [group] [name]\n"
    "\n"
    "  !backup start uart|meta\n"
    "  !backup status\n"
    "  !backup profile <A|B|C|FULL>\n"
    "  !backup custom <start> <count>\n"
    "\n"
    "  !restore plan\n"
    "  !restore arm [token] [override]\n"
    "  !restore disarm\n"
    "  !restore apply\n"
    "  !restore verify\n"
    "\n"
    "  !sd status\n"
    "  !sd rm backup|fw|all\n"
    "  !ota status\n"
  );
}

// ------------------------------------------------------------
// handleLine()
// ------------------------------------------------------------
bool Command::handleLine(Source src, const String& line) {
  if (!gCtx) return false;

  String cmd = line;
  cmd.remove(0, 1); // drop '!'
  cmd.trim();

  if (cmd.length() == 0) { showHelp(src); return true; }

  String head, tail;
  splitFirst(cmd, head, tail);

  // ==========================================================
  // SAFEGUARD GATE (blocks unsafe commands unless unsafe=ON)
  // ==========================================================
  {
    if (head.equalsIgnoreCase("unsafe")) {
      String a = tail; a.trim();

      if (a.equalsIgnoreCase("on") || a.equalsIgnoreCase("1") || a.equalsIgnoreCase("true")) {
        SafeGuard::setUnsafe(true);
        sayLn(src,
          String("unsafe=ON (auto off in ") +
          String(SafeGuard::unsafeRemainingMs() / 1000) + "s)"
        );
        return true;
      }

      if (a.equalsIgnoreCase("off") || a.equalsIgnoreCase("0") || a.equalsIgnoreCase("false")) {
        SafeGuard::setUnsafe(false);
        sayLn(src, "unsafe=OFF");
        return true;
      }

      if (a.equalsIgnoreCase("status") || !a.length()) {
        sayLn(src,
          String("unsafe=") + (SafeGuard::isUnsafe() ? "ON" : "OFF") +
          " remaining_ms=" + String(SafeGuard::unsafeRemainingMs())
        );
        return true;
      }

      sayLn(src, "Usage: !unsafe on | !unsafe off | !unsafe status");
      return true;
    }

    String why;
    String sgSub, sgArg;
    splitFirst(tail, sgSub, sgArg);
    if (!SafeGuard::allow(head, sgSub, sgArg, &why)) {
      sayLn(src, why);
      return true;
    }
  }

  // ==========================================================
  // help
  // ==========================================================
  if (head.equalsIgnoreCase("help") || head.equalsIgnoreCase("?")) {
    showHelp(src);
    return true;
  }

  // ==========================================================
  // status
  // ==========================================================
  if (head.equalsIgnoreCase("status")) {
    bool ap = gCtx->isApMode ? gCtx->isApMode() : false;
    bool saved = gCtx->haveSavedSsid ? gCtx->haveSavedSsid() : false;
    uint32_t elapsed = gCtx->apElapsedMs ? gCtx->apElapsedMs() : 0;
    uint32_t afterMs = gCtx->apTimerAfterMs ? gCtx->apTimerAfterMs() : 0;
    bool en = gCtx->apTimerEnabled ? gCtx->apTimerEnabled() : false;
    IPAddress ip = gCtx->ipNow ? gCtx->ipNow() : IPAddress(0,0,0,0);

    uint32_t baud = gCtx->uartGetBaud ? gCtx->uartGetBaud() : 0;
    bool uauto = gCtx->uartGetAuto ? gCtx->uartGetAuto() : false;

    String s;
    s += "mode="; s += (ap ? "AP" : "STA");
    s += " ip="; s += ip.toString();
    s += " saved_ssid="; s += (saved ? "yes" : "no");
    s += " ap_timer_enabled="; s += (en ? "yes" : "no");
    s += " ap_elapsed_ms="; s += String(elapsed);
    s += " ap_after_ms="; s += String(afterMs);
    s += " uart_baud="; s += String(baud);
    s += " uart_auto="; s += (uauto ? "yes" : "no");
    sayLn(src, s);
    return true;
  }

  // ==========================================================
  // reboot
  // ==========================================================
  if (head.equalsIgnoreCase("reboot") || head.equalsIgnoreCase("reset")) {
    sayLn(src, "Rebooting now...");
    delay(80);
    if (gCtx->rebootNow) gCtx->rebootNow();
    return true;
  }

  // ==========================================================
  // bp ...
  // ==========================================================
  if (head.equalsIgnoreCase("bp")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("status")) {
      String s;
      s += "bp_enabled="; s += (CFG_BP_ENABLE ? "1" : "0");
      s += " assets_loaded="; s += (BlueprintRuntime::assetsLoaded() ? "1" : "0");
      s += " prompts_loaded="; s += (BlueprintRuntime::promptsLoaded() ? "1" : "0");
      s += " gcode_loaded="; s += (BlueprintRuntime::gcodeLoaded() ? "1" : "0");
      s += " mode=";
      s += String((uint8_t)BlueprintRuntime::mode());

      String ll = bp_lastLine_ref();
      if (ll.length()) {
        s += " last_line=";
        s += ll;
      }

      String bid = bp_getKey_str("board_id");
      if (bid.length()) {
        s += " board_id=";
        s += bid;
      }

      sayLn(src, s);
      return true;
    }

    if (sub.equalsIgnoreCase("keys")) {
      sayLn(src, BlueprintRuntime::listKeysCsv());
      return true;
    }

    if (sub.equalsIgnoreCase("get")) {
      arg.trim();
      if (!arg.length()) { sayLn(src, "Usage: !bp get <key>"); return true; }

      String v = bp_getKey_str(arg);
      if (!v.length()) { sayLn(src, arg + "=(empty)"); return true; }

      sayLn(src, arg + "=" + v);
      return true;
    }

    if (sub.equalsIgnoreCase("list-scripts") || sub.equalsIgnoreCase("scripts")) {
      sayLn(src, BlueprintRuntime::listScriptsCsv());
      return true;
    }

    if (sub.equalsIgnoreCase("run")) {
      arg.trim();
      if (!arg.length()) { sayLn(src, "Usage: !bp run <name> [timeoutMs]"); return true; }

      String name, tms;
      splitFirst(arg, name, tms);

      uint32_t timeoutMs = 4000;
      if (tms.length()) (void)parseU32(tms, timeoutMs);

      bool ok = BlueprintRuntime::runScript(name, timeoutMs);
      sayLn(src, ok ? "bp run: OK" : "bp run: FAIL");
      return true;
    }

    // prompts list
    if (sub.equalsIgnoreCase("prompts")) {
      String s;
      s += "prompts_loaded="; s += (BlueprintRuntime::promptsLoaded() ? "1" : "0");
      s += " names="; s += BlueprintRuntime::listPromptsCsv();
      sayLn(src, s);
      return true;
    }

    // prompt text
    if (sub.equalsIgnoreCase("prompt")) {
      arg.trim();
      if (!arg.length()) { sayLn(src, "Usage: !bp prompt <name>"); return true; }

      String txt = BlueprintRuntime::getPromptText(arg);
      if (!txt.length()) {
        sayLn(src, String("prompt '") + arg + "' not found");
        return true;
      }
      sayLn(src, txt);
      return true;
    }

    // gcode / preset commands
    if (sub.equalsIgnoreCase("gcode")) {
      String g, n;
      splitFirst(arg, g, n);
      g.trim(); n.trim();

      if (!g.length()) {
        String s;
        s += "gcode_loaded="; s += (BlueprintRuntime::gcodeLoaded() ? "1" : "0");
        s += " groups="; s += BlueprintRuntime::listGcodeGroupsCsv();
        sayLn(src, s);
        sayLn(src, "Usage: !bp gcode <group> <name>");
        return true;
      }

      if (!n.length()) {
        String names = BlueprintRuntime::listGcodeNamesCsv(g);
        if (!names.length()) names = "(none / unknown group)";
        sayLn(src, String("group=") + g + " names=" + names);
        sayLn(src, "Usage: !bp gcode <group> <name>");
        return true;
      }

      bool ok = BlueprintRuntime::sendGcode(g, n);
      if (!ok) {
        String line2 = BlueprintRuntime::getGcodeLine(g, n);
        sayLn(src, String("bp gcode: FAIL (group=") + g + " name=" + n + " line=" + (line2.length()?line2:"(missing)") + ")");
        return true;
      }

      sayLn(src, String("bp gcode: OK (") + g + "/" + n + ")");
      return true;
    }

    // ? UPDATED usage line (includes new subcommands)
    sayLn(src,
      "Usage: !bp status | !bp keys | !bp get <key> | !bp scripts | !bp run <name> [timeoutMs] | "
      "!bp prompts | !bp prompt <name> | !bp gcode [group] [name]"
    );
    return true;
  }

  // ==========================================================
  // target ...
  // ==========================================================
  if (head.equalsIgnoreCase("target")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("reset")) {
      uint32_t ms = 200;
      if (arg.length()) (void)parseU32(arg, ms);
      if (gCtx->targetResetPulseMs) gCtx->targetResetPulseMs(ms);
      sayLn(src, String("Target reset pulsed (ms=") + ms + ")");
      return true;
    }

    if (sub.equalsIgnoreCase("fel")) {
      if (gCtx->targetEnterFel) gCtx->targetEnterFel();
      sayLn(src, "Target FEL sequence sent.");
      return true;
    }

    sayLn(src, "Usage: !target reset [ms] | !target fel");
    return true;
  }

  // ==========================================================
  // wifi ...
  // ==========================================================
  if (head.equalsIgnoreCase("wifi")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("status")) {
      return handleLine(src, String("!") + String("status"));
    }

    if (sub.equalsIgnoreCase("save")) {
      String ssid, pass;
      splitTwo(arg, ssid, pass);
      if (!ssid.length()) { sayLn(src, "Usage: !wifi save <ssid> <pass>"); return true; }
      if (gCtx->wifiSave) gCtx->wifiSave(ssid, pass);
      sayLn(src, "WiFi saved. Reboot to apply.");
      return true;
    }

    if (sub.equalsIgnoreCase("reset") || sub.equalsIgnoreCase("clear")) {
      if (gCtx->wifiReset) gCtx->wifiReset();
      sayLn(src, "WiFi cleared. Reboot to AP.");
      return true;
    }

    sayLn(src, "Usage: !wifi status | !wifi save <ssid> <pass> | !wifi reset");
    return true;
  }

  // ==========================================================
  // ap ...
  // ==========================================================
  if (head.equalsIgnoreCase("ap")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("start")) {
      if (gCtx->forceApNow) gCtx->forceApNow();
      sayLn(src, "AP start requested.");
      return true;
    }

    if (sub.equalsIgnoreCase("timer")) {
      String tsub, targ;
      splitFirst(arg, tsub, targ);

      if (tsub.equalsIgnoreCase("show")) {
        uint32_t afterMs = gCtx->apTimerAfterMs ? gCtx->apTimerAfterMs() : 0;
        bool en = gCtx->apTimerEnabled ? gCtx->apTimerEnabled() : false;
        uint32_t el = gCtx->apElapsedMs ? gCtx->apElapsedMs() : 0;
        sayLn(src, String("ap_timer_enabled=") + (en ? "1" : "0") +
                    " after_ms=" + afterMs +
                    " elapsed_ms=" + el);
        return true;
      }

      if (tsub.equalsIgnoreCase("set")) {
        uint32_t ms;
        if (!parseU32(targ, ms)) { sayLn(src, "Usage: !ap timer set <ms>"); return true; }
        if (gCtx->apTimerSetAfterMs) gCtx->apTimerSetAfterMs(ms);
        sayLn(src, String("AP timer after_ms set to ") + ms);
        return true;
      }

      if (tsub.equalsIgnoreCase("enable")) {
        if (gCtx->apTimerSetEnabled) gCtx->apTimerSetEnabled(true);
        sayLn(src, "AP timer enabled.");
        return true;
      }

      if (tsub.equalsIgnoreCase("disable")) {
        if (gCtx->apTimerSetEnabled) gCtx->apTimerSetEnabled(false);
        sayLn(src, "AP timer disabled.");
        return true;
      }

      sayLn(src, "Usage: !ap timer show | !ap timer set <ms> | !ap timer enable | !ap timer disable");
      return true;
    }

    sayLn(src, "Usage: !ap start | !ap timer ...");
    return true;
  }

  // ==========================================================
  // sta ...
  // ==========================================================
  if (head.equalsIgnoreCase("sta")) {
    String sub = tail; sub.trim();
    if (sub.equalsIgnoreCase("start")) {
      if (!gCtx->forceStaNow) { sayLn(src, "(not wired) sta start"); return true; }
      bool ok = gCtx->forceStaNow();
      sayLn(src, ok ? "STA connect started/ok." : "STA connect failed.");
      return true;
    }
    sayLn(src, "Usage: !sta start");
    return true;
  }

  // ==========================================================
  // uart ...
  // ==========================================================
  if (head.equalsIgnoreCase("uart")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("status")) {
      uint32_t baud = gCtx->uartGetBaud ? gCtx->uartGetBaud() : 0;
      bool uauto = gCtx->uartGetAuto ? gCtx->uartGetAuto() : false;
      sayLn(src, String("uart_baud=") + baud + " uart_auto=" + (uauto ? "on":"off"));
      return true;
    }

    if (sub.equalsIgnoreCase("baud")) {
      uint32_t b;
      if (!parseU32(arg, b)) { sayLn(src, "Usage: !uart baud <rate>"); return true; }
      if (gCtx->uartSetBaud) gCtx->uartSetBaud(b);
      sayLn(src, String("UART baud set to ") + b);
      return true;
    }

    if (sub.equalsIgnoreCase("auto")) {
      bool on;
      if (!parseBoolOnOff(arg, on)) { sayLn(src, "Usage: !uart auto on|off"); return true; }
      if (gCtx->uartSetAuto) gCtx->uartSetAuto(on);
      sayLn(src, String("UART auto=") + (on ? "on" : "off"));
      return true;
    }

    if (sub.equalsIgnoreCase("autodetect") || sub.equalsIgnoreCase("detect")) {
      if (gCtx->uartRunAutodetectNow) gCtx->uartRunAutodetectNow();
      sayLn(src, "UART autodetect triggered.");
      return true;
    }

    sayLn(src, "Usage: !uart status | !uart baud <rate> | !uart auto on|off | !uart autodetect");
    return true;
  }

  // ==========================================================
  // uboot / ums / env ...
  // ==========================================================
  if (head.equalsIgnoreCase("uboot")) {
    String sub = tail; sub.trim();
    if (sub.equalsIgnoreCase("prompt")) {
      bool fresh = gCtx->ubootPromptFresh ? gCtx->ubootPromptFresh() : false;
      sayLn(src, String("uboot_prompt_fresh=") + (fresh ? "yes":"no"));
      return true;
    }
    sayLn(src, "Usage: !uboot prompt");
    return true;
  }

  if (head.equalsIgnoreCase("ums")) {
    String sub = tail; sub.trim();

    if (sub.equalsIgnoreCase("start")) {
      if (gCtx->umsStart) gCtx->umsStart();
      sayLn(src, "UMS start requested.");
      return true;
    }

    if (sub.equalsIgnoreCase("clear")) {
      if (gCtx->umsClear) gCtx->umsClear();
      sayLn(src, "UMS clear requested.");
      return true;
    }

    sayLn(src, "Usage: !ums start | !ums clear");
    return true;
  }

  if (head.equalsIgnoreCase("env")) {
    String sub = tail; sub.trim();

    if (sub.equalsIgnoreCase("capture")) {
      if (gCtx->envCaptureStart) gCtx->envCaptureStart();
      sayLn(src, "Env capture started.");
      return true;
    }

    if (sub.equalsIgnoreCase("show")) {
      if (!gCtx->envLastText) { sayLn(src, "(not wired) env show"); return true; }
      String t = gCtx->envLastText();
      if (!t.length()) t = "(no env captured)";
      sayLn(src, t);
      return true;
    }

    if (sub.equalsIgnoreCase("boardid")) {
      if (!gCtx->envLastBoardId) { sayLn(src, "(not wired) env boardid"); return true; }
      String t = gCtx->envLastBoardId();
      if (!t.length()) t = "(unknown)";
      sayLn(src, String("board_id=") + t);
      return true;
    }

    if (sub.equalsIgnoreCase("layout")) {
      if (!gCtx->envLastLayoutJson) { sayLn(src, "(not wired) env layout"); return true; }
      String t = gCtx->envLastLayoutJson();
      if (!t.length()) t = "{}";
      sayLn(src, t);
      return true;
    }

    sayLn(src, "Usage: !env capture | !env show | !env boardid | !env layout");
    return true;
  }

  // ==========================================================
  // backup ...
  // ==========================================================
  if (head.equalsIgnoreCase("backup")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("start")) {
      if (arg.equalsIgnoreCase("uart")) {
        if (!gCtx->backupStartUart) { sayLn(src, "(not wired) backup start uart"); return true; }
        bool ok = gCtx->backupStartUart();
        sayLn(src, ok ? "Backup started (uart)." : "Backup start failed/busy.");
        return true;
      }
      if (arg.equalsIgnoreCase("meta")) {
        if (!gCtx->backupStartMeta) { sayLn(src, "(not wired) backup start meta"); return true; }
        bool ok = gCtx->backupStartMeta();
        sayLn(src, ok ? "Backup started (meta)." : "Backup start failed/busy.");
        return true;
      }
      sayLn(src, "Usage: !backup start uart|meta");
      return true;
    }

    if (sub.equalsIgnoreCase("status")) {
      if (gCtx->backupStatusLine) sayLn(src, gCtx->backupStatusLine());
      else sayLn(src, "(not wired) backup status");
      return true;
    }

    if (sub.equalsIgnoreCase("profile")) {
      if (!arg.length()) { sayLn(src, "Usage: !backup profile <A|B|C|FULL>"); return true; }
      if (gCtx->backupSetProfileId) gCtx->backupSetProfileId(arg);
      sayLn(src, String("Backup profile set to ") + arg);
      return true;
    }

    if (sub.equalsIgnoreCase("custom")) {
      String a, b2;
      splitTwo(arg, a, b2);
      uint32_t start=0, count=0;
      if (!parseU32(a, start) || !parseU32(b2, count)) { sayLn(src, "Usage: !backup custom <start> <count>"); return true; }
      if (gCtx->backupSetCustomRange) gCtx->backupSetCustomRange(start, count);
      sayLn(src, String("Backup custom range set start=") + start + " count=" + count);
      return true;
    }

    sayLn(src, "Usage: !backup start uart|meta | !backup status | !backup profile <A|B|C|FULL> | !backup custom <start> <count>");
    return true;
  }

  // ==========================================================
  // restore ...
  // ==========================================================
  if (head.equalsIgnoreCase("restore")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("plan")) {
      if (gCtx->restorePlan) sayLn(src, gCtx->restorePlan());
      else sayLn(src, "(not wired) restore plan");
      return true;
    }

    if (sub.equalsIgnoreCase("arm")) {
      String tok, rest;
      splitFirst(arg, tok, rest);
      bool ov = false;
      if (rest.length()) {
        String t = rest; t.trim();
        if (t.equalsIgnoreCase("override") || t.equalsIgnoreCase("1") || t.equalsIgnoreCase("true")) ov = true;
      }

      if (!gCtx->restoreArm) { sayLn(src, "(not wired) restore arm"); return true; }
      String out = gCtx->restoreArm(tok, ov);
      if (!out.length()) out = "restore arm: (no response)";
      sayLn(src, out);
      return true;
    }

    if (sub.equalsIgnoreCase("disarm")) {
      if (gCtx->restoreDisarm) gCtx->restoreDisarm();
      sayLn(src, "Restore disarmed.");
      return true;
    }

    if (sub.equalsIgnoreCase("apply")) {
      if (!gCtx->restoreApply) { sayLn(src, "(not wired) restore apply"); return true; }
      sayLn(src, gCtx->restoreApply());
      return true;
    }

    if (sub.equalsIgnoreCase("verify")) {
      if (!gCtx->restoreVerify) { sayLn(src, "(not wired) restore verify"); return true; }
      sayLn(src, gCtx->restoreVerify());
      return true;
    }

    sayLn(src, "Usage: !restore plan | !restore arm [token] [override] | !restore disarm | !restore apply | !restore verify");
    return true;
  }

  // ==========================================================
  // sd ...
  // ==========================================================
  if (head.equalsIgnoreCase("sd")) {
    String sub, arg;
    splitFirst(tail, sub, arg);

    if (sub.equalsIgnoreCase("status")) {
      if (gCtx->sdStatusJson) sayLn(src, gCtx->sdStatusJson());
      else sayLn(src, "(not wired) sd status");
      return true;
    }

    if (sub.equalsIgnoreCase("rm")) {
      if (!arg.length()) { sayLn(src, "Usage: !sd rm backup|fw|all"); return true; }
      sayLn(src, "sd rm: wire delete action via Context (not implemented here yet). Use Web UI endpoints for now.");
      return true;
    }

    sayLn(src, "Usage: !sd status | !sd rm backup|fw|all");
    return true;
  }

  // ==========================================================
  // ota ...
  // ==========================================================
  if (head.equalsIgnoreCase("ota")) {
    String sub = tail; sub.trim();

    if (sub.equalsIgnoreCase("status")) {
      bool active = gCtx->otaInProgress ? gCtx->otaInProgress() : false;
      uint32_t w = gCtx->otaWritten ? gCtx->otaWritten() : 0;
      uint32_t t = gCtx->otaTotal ? gCtx->otaTotal() : 0;
      sayLn(src, String("ota_active=") + (active ? "yes":"no") + " written=" + w + " total=" + t);
      return true;
    }

    sayLn(src, "Usage: !ota status");
    return true;
  }

  sayLn(src, "Unknown command. Use !help");
  return true;
}