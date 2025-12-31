#include "TcpUartServer.h"
#include "AppConfig.h"
#include "Debug.h"
#include "UartBridge.h"

#include <AsyncTCP.h>

DBG_REGISTER_MODULE(__FILE__);

// Avoid global static init depending on macros/objects.
// Use a pointer and construct inside begin().
static AsyncServer* sServer = nullptr;

void TcpUartServer::bootBanner() {
  printBootBanner("TCP", "Raw/Telnet UART server (single client)");
}

uint16_t TcpUartServer::port() {
  return CFG_TCP_PORT;
}

void TcpUartServer::begin(BridgeState& st) {
  if (!sServer) {
    sServer = new AsyncServer(CFG_TCP_PORT);
  }

  sServer->onClient([&st](void*, AsyncClient* c) {
    if (st.tcpClient) {
      c->write("BUSY: another client is connected.\n");
      c->close(true);
      return;
    }

    st.tcpClient = c;
    D_TCPLN("client connected");

    c->onData([&st](void*, AsyncClient* client, void* data, size_t len) {
      if (!client || client != st.tcpClient) return;
      UartBridge::serial().write((const uint8_t*)data, len);
    }, nullptr);

    c->onError([](void*, AsyncClient*, int8_t err) {
      D_TCP("error=%d\n", (int)err);
    }, nullptr);

    c->onDisconnect([&st](void*, AsyncClient* client) {
      D_TCPLN("client disconnected");
      if (st.tcpClient == client) st.tcpClient = nullptr;
    }, nullptr);
  }, nullptr);

  sServer->begin();
  D_TCP("listening on %u\n", (unsigned)CFG_TCP_PORT);
}
