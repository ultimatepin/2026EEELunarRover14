#pragma once

/*
  AppConfig.h

  v3.5 uses the lower-level WiFi101_Generic WiFiServer/WiFiClient API.

  Reason:
  On phone hotspots, browser connections can leave the higher-level
  WiFiWebServer socket wedged after a partial/empty request. This version
  keeps all responses tiny, skips unexpected requests, and force-closes every client.
*/

#include <Arduino.h>
#include <SPI.h>
#include <WiFi101_Generic.h>

// Serial monitor settings.
const unsigned long SERIAL_BAUD = 115200;
const unsigned long SERIAL_WAIT_MS = 5000;

// HTTP server port.
const uint16_t HTTP_PORT = 80;

// Board / shield labels.
#define SHIELD_TYPE "Adafruit WINC1500 Shield using WiFi101_Generic"
#define BOARD_NAME "Adafruit Metro M0"

// Example project pins.
const uint8_t LED_PIN = LED_BUILTIN;
const uint8_t SAMPLE_DIGITAL_PIN = 5;
const uint8_t SAMPLE_ANALOG_PIN = A0;

// How often maintainWiFi() checks connection health.
const unsigned long WIFI_MAINTAIN_INTERVAL_MS = 10000;

// Raw HTTP parser limits.
const unsigned long HTTP_CLIENT_TIMEOUT_MS = 350;
const size_t HTTP_REQUEST_LINE_MAX = 96;
const size_t HTTP_TARGET_MAX = 64;
