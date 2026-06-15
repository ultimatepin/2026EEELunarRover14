#pragma once

#include "AppConfig.h"

/*
  Network.h
  =========

  Declarations for WiFi connection helpers.

  Django analogy:
    This is like declaring service/helper functions used by app startup and
    health maintenance.
*/

/* Convert an IPAddress object into dotted text like "172.20.10.3". */
String ipAddressToString(const IPAddress &ip);

/* Blocking startup connection. Called once from setup(). */
void connectToWiFiBlocking();

/* Periodic non-blocking WiFi health check. Called repeatedly from loop(). */
void maintainWiFi();

/* Print current IP and RSSI to Serial Monitor. */
void printWiFiStatus();
