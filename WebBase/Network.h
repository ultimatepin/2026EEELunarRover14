#pragma once

#include "AppConfig.h"

String ipAddressToString(const IPAddress &ip);

void connectToWiFiBlocking();
void maintainWiFi();
void printWiFiStatus();
