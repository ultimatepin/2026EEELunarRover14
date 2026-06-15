#include "AppConfig.h"
#include "Secrets.h"
#include "Network.h"

namespace
{
  /*
    File-local variable.

    `namespace { ... }` in C++ makes this variable private to Network.cpp.
    This avoids accidental name conflicts with other files.
  */
  unsigned long lastWiFiMaintainMs = 0;
}

void connectToWiFiBlocking()
{
  /*
    WiFi.status()

    Returns the current WiFi state.
    Important states:
    - WL_CONNECTED: successfully connected to router
    - WL_NO_SHIELD: WiFi shield not detected
    - WL_IDLE_STATUS: not connected yet / idle
  */
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("ERROR: WINC1500/WiFi101 shield not detected."));
    Serial.println(F("Check shield seating, SPI pins, and power."));
    while (true)
    {
      delay(1000);
    }
  }

  Serial.print(F("Firmware version: "));
  Serial.println(WiFi.firmwareVersion());

  Serial.print(F("Connecting to WiFi SSID: "));
  Serial.println(WIFI_SSID);

  /*
    WiFi.begin(ssid, password)

    Parameters:
    - ssid: WiFi network name
    - password: WPA/WPA2 password

    Principle:
    This asks the WINC1500 module to join your router's network.
    Your Metro M0 does not directly speak WiFi; it talks to the WINC1500 over SPI.
  */
  int status = WL_IDLE_STATUS;

  while (status != WL_CONNECTED)
  {
    status = WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print(F("."));
    delay(1000);
  }

  Serial.println();
  Serial.println(F("WiFi connected."));
  printWiFiStatus();
}

void maintainWiFi()
{
  /*
    Non-blocking periodic check.

    millis() is the number of milliseconds since boot.
    Unlike delay(), this lets loop() keep running and lets the server stay responsive.
  */
  const unsigned long now = millis();

  if (now - lastWiFiMaintainMs < WIFI_MAINTAIN_INTERVAL_MS)
  {
    return;
  }

  lastWiFiMaintainMs = now;

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("WiFi disconnected. Reconnecting..."));
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

void printWiFiStatus()
{
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());

  Serial.print(F("Signal strength RSSI: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}
