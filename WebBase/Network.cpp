#include "Network.h"
#include "Secrets.h"

/*
  Network.cpp
  ===========

  WiFi connection and reconnection logic.

  This file deliberately does not know about routes like /api/ping. It only
  knows how to connect/reconnect the WINC1500 shield and restart the raw server
  if WiFi comes back.
*/

/*
  This server object is created in the .ino file.
  We need it here so maintainWiFi() can call server.begin() after reconnecting.
*/
extern WiFiServer server;

namespace
{
  /* Last time maintainWiFi() performed a check. */
  unsigned long lastWiFiMaintainMs = 0;

  /*
    Startup WiFi timeout.

    If credentials are wrong or the hotspot is unavailable, the board prints an
    error instead of dot-looping forever.
  */
  const unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
}

String ipAddressToString(const IPAddress &ip)
{
  /*
    Some Arduino cores support ip.toString(), but this byte-by-byte version is
    safer across WiFi101/WINC1500 variants.

    IPAddress behaves like a 4-byte array:
      ip[0], ip[1], ip[2], ip[3]
  */
  return String(ip[0]) + "." +
         String(ip[1]) + "." +
         String(ip[2]) + "." +
         String(ip[3]);
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
  /*
    Check that the WINC1500 shield is detected.
    If this fails, common causes are shield seating, power, or SPI/pin issues.
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

  int status = WL_IDLE_STATUS;
  const unsigned long connectStartMs = millis();

  /*
    Keep trying until connected or timeout.

    Parameter source:
      WIFI_SSID and WIFI_PASS come from Secrets.h.

    Hotspot note:
      Many phone hotspots are 2.4 GHz compatible, but settings differ. If this
      times out, check hotspot visibility and password first.
  */
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
  /*
    Non-blocking periodic check.

    Django analogy:
      This is a tiny health-check loop, not a request handler.

    Arduino principle:
      Use millis() timing instead of delay() so the server keeps responding.
  */
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

    /*
      Important for this project:
      After WiFi reconnects, restart the listening socket. Otherwise the board
      may be connected to WiFi but not accepting HTTP requests.
    */
    server.begin();
    Serial.println(F("Raw HTTP server restarted."));
  }
  else
  {
    Serial.print(F("Reconnect status: "));
    Serial.println(status);
  }
}
