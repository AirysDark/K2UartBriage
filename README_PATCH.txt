K2UartBriage - Web UI Pages Update (Web_pages.h / Web_pages.cpp)

What this ZIP contains
- include/Web_pages.h
- src/Web_pages.cpp

What this update does
- Adds top navigation tabs: UART Rescue / Firmware Utilities / Diagnostics / Console
- Loads hostname + app identity from GET /api/sys/info
- Loads tab model + subtitle from GET /api/nav
- Keeps console working via WebSocket /ws (send adds \n)

Required firmware API additions (in your Main.cpp/WebUi.cpp where you register routes)
1) Add /api/sys/info (JSON with app + hostname)
2) Add /api/nav (JSON with items + subtitle)
3) Add wifi.hostname to existing /api/status so UI can show hostname everywhere

If you already added those endpoints, you're done.
If not, paste the handlers from the chat message into your route setup.

Install
- Copy include/Web_pages.h -> your project's include/Web_pages.h
- Copy src/Web_pages.cpp   -> your project's src/Web_pages.cpp
- Build + flash.
