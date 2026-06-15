#pragma once

/*
  AppConfig.h
  ===========

  This is the central settings file for the project.

  Django analogy:
  - Think of this like a very small settings.py.
  - Other files include this file when they need project-wide constants,
    hardware pin choices, library imports, or timeout values.

  Arduino/C++ note:
  - #pragma once prevents this header from being included more than once
    during compilation.
  - const values are compile-time-ish configuration values stored by the
    program and reused across files.
*/

#include <Arduino.h>          // Core Arduino types: String, millis(), pinMode(), etc.
#include <SPI.h>              // The WINC1500 WiFi shield talks to the Metro M0 using SPI.
#include <WiFi101_Generic.h>  // WiFi library for WINC1500-class hardware.

/*
  Serial monitor settings
  -----------------------

  SERIAL_BAUD:
    Speed used by Serial Monitor. Arduino IDE Serial Monitor must match this.

  SERIAL_WAIT_MS:
    Metro M0 has native USB serial. Right after reset, Serial may not be ready.
    setup() waits up to this many milliseconds so you can see startup logs.
*/
const unsigned long SERIAL_BAUD = 115200;
const unsigned long SERIAL_WAIT_MS = 5000;

/*
  HTTP server port
  ----------------

  Port 80 is the normal unencrypted HTTP port.

  Browser usage:
    http://172.20.10.3/

  If you changed this to 8080, you would need:
    http://172.20.10.3:8080/
*/
const uint16_t HTTP_PORT = 80;

/*
  Human-readable labels only.
  These are useful in logs/readmes and do not affect hardware setup.
*/
#define SHIELD_TYPE "Adafruit WINC1500 Shield using WiFi101_Generic"
#define BOARD_NAME "Adafruit Metro M0"

/*
  Pin choices
  -----------

  LED_PIN:
    Built-in LED on the Metro M0 board.

  SAMPLE_DIGITAL_PIN:
    D5 is configured as INPUT_PULLUP. If nothing is connected, it normally reads HIGH.
    If you connect D5 to GND, it reads LOW.

  SAMPLE_ANALOG_PIN:
    A0 is read with analogRead(). Replace this later with your real sensor input.
*/
const uint8_t LED_PIN = LED_BUILTIN;
const uint8_t SAMPLE_DIGITAL_PIN = 5;
const uint8_t SAMPLE_ANALOG_PIN = A0;

/*
  WiFi maintenance interval
  -------------------------

  maintainWiFi() checks connection health periodically, not on every loop pass.

  Why not every loop?
    loop() runs thousands of times per second. Calling WiFi.status() too often
    wastes time and can make the HTTP server less responsive.
*/
const unsigned long WIFI_MAINTAIN_INTERVAL_MS = 10000;

/*
  Raw HTTP parser limits
  ----------------------

  This project does not use a high-level web framework. It reads the first HTTP
  request line manually, for example:

    GET /api/ping HTTP/1.1

  HTTP_CLIENT_TIMEOUT_MS:
    Maximum time to wait for that first request line.
    Kept short because phone hotspots/browsers can open partial sockets.
    If a client stalls, we close it quickly so the board can serve the next request.

  HTTP_REQUEST_LINE_MAX:
    Maximum number of characters stored from the first request line.
    Small embedded boards avoid large buffers.

  HTTP_TARGET_MAX:
    Maximum length of the route/target part, e.g. /api/led?value=1.
*/
const unsigned long HTTP_CLIENT_TIMEOUT_MS = 350;
const size_t HTTP_REQUEST_LINE_MAX = 96;
const size_t HTTP_TARGET_MAX = 64;
