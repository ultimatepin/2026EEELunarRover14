/*
  MetroM0_WINC1500_WebBase.ino

  Main Arduino sketch file.

  Django analogy:
  - setup() is like starting your Django development server.
  - loop() is like the server event loop: it keeps checking for incoming requests.
  - server.handleClient() is the key line that lets the microcontroller accept and answer HTTP requests.

  Important:
  WiFiWebServer's .hpp implementation file should be included only once, in the main .ino.
  This follows the library's own multi-file project pattern to avoid linker errors.
*/

#include "AppConfig.h"
#include "Secrets.h"
#include "Network.h"
#include "BoardState.h"
#include "WebRoutes.h"

// Include the implementation only once in the whole project.
#include <WiFiWebServer.hpp>

// Global HTTP server object.
// Parameter: 80 = standard HTTP port.
// Browser URL will look like: http://192.168.x.x/
WiFiWebServer server(HTTP_PORT);

void setup()
{
  Serial.begin(SERIAL_BAUD);

  // On native USB boards like Metro M0, Serial may take a moment to open.
  // The timeout prevents the sketch from freezing forever when powered by battery.
  while (!Serial && millis() < SERIAL_WAIT_MS)
  {
  }

  Serial.println();
  Serial.println(F("Starting Metro M0 + WINC1500 web base"));

  setupBoardPins();
  connectToWiFiBlocking();

  configureWebRoutes();

  server.begin();

  Serial.print(F("HTTP server started. Open: http://"));
  Serial.print(WiFi.localIP());
  Serial.println(F("/"));
}

void loop()
{
  /*
    This must run very often.

    Principle:
    WiFiWebServer is not asynchronous. It does not run in the background.
    Every time loop() runs, handleClient() checks whether a browser has requested a page/API route.
    Avoid long delay() calls in loop(), because they make the web page feel slow.
  */
  server.handleClient();

  /*
    Non-blocking WiFi maintenance.
    This checks occasionally whether WiFi disconnected and tries to reconnect.
  */
  maintainWiFi();
}
