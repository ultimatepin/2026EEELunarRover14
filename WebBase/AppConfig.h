#pragma once

/*
  AppConfig.h

  Central compile-time configuration.

  This file is included by several .cpp files, so it should contain:
  - constants
  - #define options
  - lightweight shared configuration

  It should NOT define large global objects like `WiFiWebServer server`;
  that object is defined once in the .ino file.
*/

#include <Arduino.h>

/*
  WiFiWebServer library selection.

  Your hardware:
  - Adafruit Metro M0 = SAMD21 microcontroller
  - Adafruit WINC1500 WiFi shield = WiFi101/WINC1500 class hardware

  These flags must appear before including <WiFiWebServer.h>.
*/
#define DEBUG_WIFI_WEBSERVER_PORT Serial

// Debug levels: 0 = quiet, 4 = verbose.
// Start low; verbose logs can slow small boards.
#define _WIFI_LOGLEVEL_ 1
#define _WIFININA_LOGLEVEL_ 0

// Force WiFi101/WINC1500 mode.
// Do NOT use WiFiNINA for the WINC1500 shield.
#define USE_WIFI_NINA false
#define USE_WIFI101 true
#define USE_WIFI_CUSTOM false

#define SHIELD_TYPE "Adafruit WINC1500 Shield using WiFi101"
#define BOARD_NAME "Adafruit Metro M0"

/*
  Include the class declarations for WiFiWebServer, WiFi, IPAddress, etc.

  The .hpp implementation is included only once in the main .ino file.
*/
#include <WiFiWebServer.h>

// Serial monitor settings.
const unsigned long SERIAL_BAUD = 115200;
const unsigned long SERIAL_WAIT_MS = 5000;

// HTTP server port.
const uint16_t HTTP_PORT = 80;

/*
  Example project pins.

  Avoid pins used by the WINC1500 shield:
  commonly CS/IRQ/RST/EN and SPI-related pins.

  D5 is chosen as a simple future digital input example.
  A0 is chosen as a simple future analog input example.
*/
const uint8_t LED_PIN = LED_BUILTIN;
const uint8_t SAMPLE_DIGITAL_PIN = 5;
const uint8_t SAMPLE_ANALOG_PIN = A0;

// How often maintainWiFi() checks connection health.
const unsigned long WIFI_MAINTAIN_INTERVAL_MS = 10000;
