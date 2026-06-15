#pragma once

/*
  Network.h

  Functions responsible for WiFi connection.

  Keeping WiFi logic separate from web-route logic makes it easier to later add:
  - reconnect handling
  - static IP
  - mDNS
  - WiFi status page
*/

void connectToWiFiBlocking();
void maintainWiFi();
void printWiFiStatus();
