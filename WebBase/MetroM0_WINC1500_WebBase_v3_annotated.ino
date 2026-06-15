/*
  MetroM0_WINC1500_WebBase_v3_annotated
  ======================================

  This is the Arduino sketch entry point.

  Django analogy:
  - setup() is like process startup: load settings, initialize hardware,
    connect services, start the server.
  - loop() is like a very small event loop: repeatedly check for one HTTP
    client, then do maintenance.

  Important design choice:
  This version uses raw WiFiServer/WiFiClient instead of WiFiWebServer.
  That gives us direct control over fragile hotspot behavior:
    1. accept a client,
    2. read one request line,
    3. allow only known GET routes,
    4. send a small response,
    5. force-close the socket.
*/

#include "AppConfig.h"
#include "Secrets.h"
#include "Network.h"
#include "BoardState.h"
#include "WebRoutes.h"

/*
  Global server object
  --------------------

  WiFiServer listens for incoming TCP connections on HTTP_PORT.

  Django analogy:
    This is closer to the low-level socket server underneath Django, not Django
    itself. WebRoutes.cpp acts like the tiny router/view layer.

  extern usage:
    Other .cpp files refer to this object using:
      extern WiFiServer server;
    That means "this object exists in another file; use the same one." 
*/
WiFiServer server(HTTP_PORT);

void setup()
{
  /*
    Start USB serial logging.
    The Serial Monitor must be set to SERIAL_BAUD, currently 115200.
  */
  Serial.begin(SERIAL_BAUD);

  /*
    Wait briefly for Serial Monitor to attach.
    This is especially helpful on Metro M0 because USB serial can appear after reset.
  */
  while (!Serial && millis() < SERIAL_WAIT_MS)
  {
  }

  Serial.println();
  Serial.println(F("Starting Metro M0 + WINC1500 web base v3 annotated"));

  /*
    Initialize hardware pins before serving web requests.
    This ensures LED/API state is valid as soon as the first request arrives.
  */
  setupBoardPins();

  /*
    Connect to the WiFi/hotspot.
    This function blocks until connected, or stops with an error after timeout.
  */
  connectToWiFiBlocking();

  /*
    Start listening for HTTP clients.
    After this line, browser requests can reach the board.
  */
  server.begin();

  Serial.print(F("Raw HTTP server started. Open: http://"));
  Serial.print(ipAddressToString(WiFi.localIP()));
  Serial.println(F("/"));
  Serial.println(F("Minimal test route: /api/ping"));
  Serial.println(F("Dashboard route: /dashboard"));
}

void loop()
{
  /*
    Handle at most one pending HTTP client.

    This function returns quickly if no browser/client is waiting.
    Keeping it non-blocking is essential: if loop() gets stuck here, WiFi
    maintenance and later requests cannot run.
  */
  handleHttpClient();

  /*
    Periodically check whether WiFi is still connected.
    If it reconnected, Network.cpp restarts the raw HTTP server socket too.
  */
  maintainWiFi();
}
