/*
  EEELunarRover_RawWiFi_Integrated
  ================================

  Integrated rover build:
  - annotated raw WiFiServer/WiFiClient networking from the stable base,
  - prototype UART/IR/ultrasonic/BMM150 rock classification,
  - prototype 4-motor mecanum commands,
  - dashboard at /dashboard.

  Loop priority is deliberate:
    1. handle at most one HTTP request and close the socket,
    2. run non-blocking rover tasks,
    3. occasionally maintain WiFi.

  This lets rapid /motor requests be processed while sensor classification keeps
  updating in the background.
*/

#include "AppConfig.h"
#include "Secrets.h"
#include "Network.h"
#include "RoverState.h"
#include "WebRoutes.h"

WiFiServer server(HTTP_PORT);

void setup()
{
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < SERIAL_WAIT_MS)
  {
  }

  Serial.println();
  Serial.println(F("Starting EEELunarRover raw WiFi integrated build"));
  Serial.print(F("Board: "));
  Serial.println(F(BOARD_NAME));
  Serial.print(F("WiFi: "));
  Serial.println(F(SHIELD_TYPE));

  setupRoverHardware();
  connectToWiFiBlocking();

  server.begin();

  Serial.print(F("HTTP server started. Open: http://"));
  Serial.print(ipAddressToString(WiFi.localIP()));
  Serial.println(F("/dashboard"));
  Serial.println(F("Test routes: /api/ping, /sensor, /motor?cmd=stop"));
}

void loop()
{
  handleHttpClient();
  runRoverTasks();
  maintainWiFi();
}
