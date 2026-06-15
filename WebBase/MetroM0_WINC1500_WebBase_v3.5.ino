/*
  MetroM0_WINC1500_WebBase_v3.5

  Hotspot-stability version.

  This version uses WiFi101_Generic's raw WiFiServer/WiFiClient API instead of
  WiFiWebServer. It force-closes every HTTP client after every response so a
  browser/hotspot cannot leave the one-client server stuck on a stale socket.
*/

#include "AppConfig.h"
#include "Secrets.h"
#include "Network.h"
#include "BoardState.h"
#include "WebRoutes.h"

WiFiServer server(HTTP_PORT);

void setup()
{
  Serial.begin(SERIAL_BAUD);

  while (!Serial && millis() < SERIAL_WAIT_MS)
  {
  }

  Serial.println();
  Serial.println(F("Starting Metro M0 + WINC1500 web base v3.5 raw API-only server"));

  setupBoardPins();
  connectToWiFiBlocking();

  server.begin();

  Serial.print(F("Raw HTTP server started. Open: http://"));
  Serial.print(ipAddressToString(WiFi.localIP()));
  Serial.println(F("/"));
  Serial.println(F("Minimal test route: /api/ping"));
}

void loop()
{
  handleHttpClient();
  maintainWiFi();
}
