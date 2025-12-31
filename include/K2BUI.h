#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h> 

// ============================================================
// K2BUI - Web UI + Hidden WS console for K2UartBriage
//  - Hidden WS endpoint for UART + iCommand + iBP
//  - No UART core changes required (uses injected callbacks)
// ============================================================

class K2BUI {
public:
  struct Callbacks {
    // Send bytes to UART (TX)
    void (*uart_write)(const uint8_t* data, size_t len) = nullptr;

    // Optional: execute local "! command" handler (iCommand)
    // Return a response string (or JSON string) to send back.
    String (*icommand_exec)(const String& line) = nullptr;

    // Optional: iBP command handler (blueprint/profile)
    // Input: JSON payload string; Output: JSON response string.
    String (*ibp_exec)(const String& jsonPayload) = nullptr;

    // Optional: provide a token (instead of hardcoding)
    // Called when the WS client attempts auth.
    bool (*auth_check)(const String& token) = nullptr;
  };

  // Begin UI/WS glue. Provide the main AsyncWebServer.
  static void begin(AsyncWebServer& server, const Callbacks& cb);

  // Pump WS housekeeping and (optionally) any internal buffering.
  static void tick();

  // Feed UART RX bytes into WS (binary broadcast).
  // Call this from your bridge layer where you already have UART RX bytes.
  static void onUartRx(const uint8_t* data, size_t len);

  // For UI: expose a simple health endpoint if you want.
  static void addDebugEndpoints(AsyncWebServer& server);

private:
  static void onWsEvent(
    AsyncWebSocket* server,
    AsyncWebSocketClient* client,
    AwsEventType type,
    void* arg,
    uint8_t* data,
    size_t len
  );

  static bool isAuthed(uint32_t id);
  static void setAuthed(uint32_t id, bool v);

  static bool handleText(AsyncWebSocketClient* client, const String& msg);
  static void sendJsonErr(AsyncWebSocketClient* client, const char* code, const char* msg);

private:
  static AsyncWebSocket* _ws;
  static Callbacks _cb;

  static const char* WS_PATH;     // "/_sys/ws" by default
};