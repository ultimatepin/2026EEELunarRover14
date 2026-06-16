#pragma once

/*
  AppConfig.h
  ===========

  Central configuration for the integrated EEELunarRover build.

  The networking skeleton is intentionally inherited from the annotated raw
  WiFiServer base because it proved more stable on WINC1500/phone-hotspot
  setups than WiFiWebServer. Sensor and motor constants are kept here so the
  hardware-facing code stays easy to audit.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi101_Generic.h>
#include <DFRobot_BMM150.h>       // Install the DFRobot_BMM150 library in Arduino IDE.

// ========================== BUILD MODE ==========================
// 0 = real rover hardware, 1 = software-generated sensor values.
#define ROVER_SIMULATED_MODE 0

// ========================== SERIAL / NETWORK ==========================
const unsigned long SERIAL_BAUD = 115200;
const unsigned long SERIAL_WAIT_MS = 1200;
const uint16_t HTTP_PORT = 80;

// Radio age receiver: Metro M0 hardware UART RX is D0.
// Expected packet is #ddd, e.g. #123. This patched build also accepts plain ddd
// and normalises it to #ddd so the dashboard age field still works.
const uint8_t PIN_RADIO_RX = 0;
const unsigned long RADIO_BAUD = 600;
const bool RADIO_ACCEPT_PLAIN_3_DIGIT_AGE = true;
const bool RADIO_RAW_SERIAL_DEBUG = true; // Set false after debugging to reduce Serial Monitor spam.

#define SHIELD_TYPE "Adafruit WINC1500 Shield using WiFi101_Generic"
#define BOARD_NAME "Adafruit Metro M0"

// Optional fixed EEERover IP. Keep 0 for DHCP/phone hotspot. Set to your group
// number, e.g. 14, to request 192.168.0.15 before WiFi.begin().
const uint8_t EEEROVER_GROUP_NUMBER = 0;

const unsigned long WIFI_MAINTAIN_INTERVAL_MS = 10000;
const unsigned long HTTP_CLIENT_TIMEOUT_MS = 500;
const size_t HTTP_REQUEST_LINE_MAX = 128;
const size_t HTTP_TARGET_MAX = 96;

// ========================== PINS ==========================
const uint8_t LED_PIN = LED_BUILTIN;

// WINC1500 shield / SPI pins are reserved. Do not assign rover I/O to these.
// Prototype note: D5, D7, D10 are used by the WINC1500 shield control signals.
// D13 is usable but also drives the onboard LED.

// Sensors from the prototype.
const uint8_t PIN_IR_INTERRUPT = 9;   // IR pulse input, interrupt-capable.
// User hardware intentionally uses D1 for the 40 kHz ultrasonic detector.
// Metro M0 D1 is also Serial1 TX; keep this only if your wiring/testing confirms
// that the ultrasonic detector works on D1 in your rover setup.
const uint8_t PIN_ULTRASONIC   = 1;   // Digital 40 kHz detector input, original D1 wiring.
// Serial1 RX on D0 receives radio age strings at RADIO_BAUD.
// BMM150 uses I2C: SDA/SCL on the Metro M0 Wire pins.

// Motor driver pins from the prototype. Confirm against the physical wiring.
const uint8_t PIN_FL_DIR = 8;
const uint8_t PIN_FL_PWM = 11;
const uint8_t PIN_FR_DIR = 2;
const uint8_t PIN_FR_PWM = 3;
const uint8_t PIN_RL_DIR = 12;
const uint8_t PIN_RL_PWM = 13;
const uint8_t PIN_RR_DIR = 4;
const uint8_t PIN_RR_PWM = 6;

// ========================== SENSOR CLASSIFICATION ==========================
const float LAMBDA_HIGH = 547.0f;       // Basaltoid / Lunarite IR pulse rate, s^-1.
const float LAMBDA_LOW  = 312.0f;       // Gravion / Regolix IR pulse rate, s^-1.
const unsigned long IR_WINDOW_MS = 200; // Minimum intended IR count window.

// BMM150 Z-axis convention: +Z = UP, -Z = DOWN. Flip in RoverState.cpp if your
// physical sensor mounting produces the opposite polarity.
const float BMM150_DEADZONE_UT = 30.0f;
const float BMM150_MAX_UT = 500.0f;

// ========================== NON-BLOCKING TASK INTERVALS ==========================
const unsigned long SENSOR_POLL_INTERVAL_MS = 500;
const unsigned long SERIAL_PRINT_INTERVAL_MS = 1000; // Only printed when USB Serial is attached.
const unsigned long SIM_UART_INTERVAL_MS = 3000;
const unsigned long MOTOR_COMMAND_TIMEOUT_MS = 2500; // Safe stop if dashboard keepalive disappears.
const unsigned long BMM150_RETRY_INTERVAL_MS = 2000; // Retry magnetometer init after standalone power-up.
