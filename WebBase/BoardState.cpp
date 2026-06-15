#include "BoardState.h"
#include "Network.h"

/*
  BoardState.cpp
  ==============

  This file contains hardware-facing code:
  - configure pins,
  - read pins/sensors,
  - track LED state,
  - format board state as JSON.

  Keeping this separate from WebRoutes.cpp makes the project easier to extend.
  Later, if you add a temperature sensor, put the sensor-reading logic here and
  expose the result through BoardSnapshot / JSON helpers.
*/

namespace
{
  /*
    File-private variable
    ---------------------

    namespace { ... } in a .cpp file makes names private to this file.

    Django/Python analogy:
      This is like a module-level private variable, except C++ enforces file
      visibility more strongly.

    Why store LED state ourselves?
      digitalWrite(LED_PIN, HIGH/LOW) changes the pin output, but there is no
      portable Arduino function that says "what value did I last write?".
  */
  bool ledIsOn = false;
}

void setupBoardPins()
{
  /*
    pinMode(pin, mode)
    ------------------

    OUTPUT:
      The microcontroller drives the pin HIGH or LOW.

    INPUT_PULLUP:
      The microcontroller reads the pin, and also weakly connects it to 3.3V
      internally. This prevents the input from "floating" randomly.

    Practical D5 wiring:
      - Nothing connected: digitalRead(D5) usually returns HIGH.
      - Button/switch connecting D5 to GND: digitalRead(D5) returns LOW.
  */
  pinMode(LED_PIN, OUTPUT);
  pinMode(SAMPLE_DIGITAL_PIN, INPUT_PULLUP);

  /*
    Start LED off so hardware state and our stored ledIsOn variable agree.
  */
  setLedState(false);
}

bool getLedState()
{
  return ledIsOn;
}

void setLedState(bool on)
{
  /*
    Parameter:
      on = true   -> turn LED on
      on = false  -> turn LED off

    This function updates both:
      1. software state: ledIsOn
      2. hardware output: digitalWrite(...)
  */
  ledIsOn = on;

  /*
    HIGH usually means 3.3V on Metro M0.
    LOW means 0V/GND.

    For LED_BUILTIN on Metro M0, HIGH normally turns the LED on.
  */
  digitalWrite(LED_PIN, ledIsOn ? HIGH : LOW);
}

BoardSnapshot readBoardSnapshot()
{
  /*
    Collect all board readings into one value.

    Django analogy:
      This is like building the context object before returning JsonResponse().
  */
  BoardSnapshot snapshot;

  snapshot.ledOn = getLedState();

  /*
    analogRead(A0)
    --------------

    Reads voltage on A0 and returns a number.
    On many Arduino boards this is 0..1023 by default, but exact resolution can
    vary by board/core configuration.
  */
  snapshot.analogA0 = analogRead(SAMPLE_ANALOG_PIN);

  /*
    digitalRead(D5)
    ---------------

    Because D5 is INPUT_PULLUP:
      HIGH = open/not grounded
      LOW  = connected to GND
  */
  snapshot.digitalD5 = digitalRead(SAMPLE_DIGITAL_PIN);

  /*
    millis() is time since boot in milliseconds.
    It eventually overflows after many days, but subtraction-based timing still
    works correctly for normal Arduino patterns.
  */
  snapshot.uptimeMs = millis();

  /*
    WiFi.RSSI() returns signal strength in dBm.
    Example: -35 is stronger than -80.
  */
  snapshot.wifiRssi = WiFi.RSSI();

  /*
    WiFi.localIP() returns an IPAddress object.
    Network.cpp converts it to text when making JSON.
  */
  snapshot.ip = WiFi.localIP();

  return snapshot;
}

String boardFullStateToJson(const BoardSnapshot &snapshot, unsigned long requestCount)
{
  /*
    Full JSON response for /api/state.

    Parameters:
      snapshot:      board values collected by readBoardSnapshot().
      requestCount:  total-ish count of handled known requests.

    const BoardSnapshot & means:
      - pass the snapshot by reference, so it is not copied,
      - const means this function promises not to modify it.
  */
  String json = "{";

  json += "\"led_on\":";
  json += snapshot.ledOn ? "true" : "false";

  json += ",\"analog_a0\":";
  json += snapshot.analogA0;

  json += ",\"digital_d5\":";
  json += snapshot.digitalD5;

  json += ",\"uptime_ms\":";
  json += snapshot.uptimeMs;

  json += ",\"wifi_rssi\":";
  json += snapshot.wifiRssi;

  json += ",\"ip\":\"";
  json += ipAddressToString(snapshot.ip);
  json += "\"";

  json += ",\"request_count\":";
  json += requestCount;

  json += "}";

  return json;
}

String boardStatusToJson(const BoardSnapshot &snapshot, unsigned long requestCount)
{
  /*
    Status-only JSON for /api/status.

    This intentionally excludes led_on. The dashboard polls /api/status every
    few seconds, but LED state is updated only after a button request. This
    avoids mixing "live sensor polling" with "LED command result".
  */
  String json = "{";

  json += "\"analog_a0\":";
  json += snapshot.analogA0;

  json += ",\"digital_d5\":";
  json += snapshot.digitalD5;

  json += ",\"uptime_ms\":";
  json += snapshot.uptimeMs;

  json += ",\"wifi_rssi\":";
  json += snapshot.wifiRssi;

  json += ",\"ip\":\"";
  json += ipAddressToString(snapshot.ip);
  json += "\"";

  json += ",\"request_count\":";
  json += requestCount;

  json += "}";

  return json;
}

String boardLedToJson()
{
  /*
    Small LED response for /api/led.

    Example:
      {"led_on":true}
  */
  String json = "{";
  json += "\"led_on\":";
  json += getLedState() ? "true" : "false";
  json += "}";

  return json;
}
