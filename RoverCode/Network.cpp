#include "Network.h"
#include "Secrets.h"

/*
  Network.cpp
  ===========
  Stable WINC1500 connection logic. The HTTP router lives elsewhere; this file
  only connects/reconnects WiFi and restarts the listening socket after recovery.
*/

extern WiFiServer server;

namespace
{
  unsigned long lastWiFiMaintainMs = 0;
  const unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
}

String ipAddressToString(const IPAddress &ip)
{
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

void printWiFiStatus()
{
  Serial.print(F("IP address: "));
  Serial.println(ipAddressToString(WiFi.localIP()));
  Serial.print(F("Signal strength RSSI: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void connectToWiFiBlocking()
{
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

  if (EEEROVER_GROUP_NUMBER > 0)
  {
    IPAddress fixedIp(192, 168, 0, EEEROVER_GROUP_NUMBER + 1);
    WiFi.config(fixedIp);
    Serial.print(F("Requested fixed EEERover IP: "));
    Serial.println(ipAddressToString(fixedIp));
  }

  Serial.print(F("Connecting to WiFi SSID: "));
  Serial.println(WIFI_SSID);

  int status = WL_IDLE_STATUS;
  const unsigned long connectStartMs = millis();

  while (status != WL_CONNECTED)
  {
    status = WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("."));
    delay(1000);

    if (millis() - connectStartMs > WIFI_CONNECT_TIMEOUT_MS)
    {
      Serial.println();
      Serial.println(F("ERROR: WiFi connection timed out."));
      Serial.println(F("Check SSID/password, hotspot visibility, and 2.4 GHz compatibility."));
      while (true)
      {
        delay(1000);
      }
    }
  }

  Serial.println();
  Serial.println(F("WiFi connected."));
  printWiFiStatus();
}

void maintainWiFi()
{
  const unsigned long now = millis();

  if (now - lastWiFiMaintainMs < WIFI_MAINTAIN_INTERVAL_MS)
  {
    return;
  }

  lastWiFiMaintainMs = now;

  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  Serial.println(F("WiFi disconnected. Reconnecting..."));
  const int status = WiFi.begin(WIFI_SSID, WIFI_PASS);

  if (status == WL_CONNECTED)
  {
    Serial.println(F("WiFi reconnected."));
    printWiFiStatus();
    server.begin();
    Serial.println(F("Raw HTTP server restarted."));
  }
  else
  {
    Serial.print(F("Reconnect status: "));
    Serial.println(status);
  }
}
