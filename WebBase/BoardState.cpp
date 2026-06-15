#include "BoardState.h"
#include "Network.h"

namespace
{
  /*
    Private state variable.

    We store LED state ourselves because digitalWrite() changes the pin,
    but Arduino does not provide a clean universal "what did I last write?" API.
  */
  bool ledIsOn = false;
}

void setupBoardPins()
{
  /*
    pinMode(pin, mode)

    Parameters:
    - pin: Arduino pin number, such as LED_BUILTIN, D5, A0
    - mode:
      - OUTPUT: the microcontroller drives voltage HIGH/LOW
      - INPUT: the pin reads external voltage
      - INPUT_PULLUP: the pin reads input and also enables an internal pull-up resistor

    INPUT_PULLUP principle:
    A floating input pin randomly reads HIGH/LOW.
    INPUT_PULLUP weakly connects the pin to 3.3V internally, so it reads HIGH by default.
    If you connect the pin to GND through a button/switch, it reads LOW.
  */
  pinMode(LED_PIN, OUTPUT);
  pinMode(SAMPLE_DIGITAL_PIN, INPUT_PULLUP);

  setLedState(false);
}

bool getLedState()
{
  return ledIsOn;
}

void setLedState(bool on)
{
  ledIsOn = on;

  /*
    digitalWrite(pin, value)

    HIGH usually means 3.3V on Metro M0.
    LOW means 0V/GND.

    For LED_BUILTIN:
    HIGH usually turns the LED on.
  */
  digitalWrite(LED_PIN, ledIsOn ? HIGH : LOW);
}

BoardSnapshot readBoardSnapshot()
{
  BoardSnapshot snapshot;

  snapshot.ledOn = getLedState();

  /*
    analogRead(A0)

    Reads voltage on A0 and converts it to a number.
    On many Arduino boards the default range is 0..1023.
    Later, you can replace this with your real sensor-processing logic.
  */
  snapshot.analogA0 = analogRead(SAMPLE_ANALOG_PIN);

  /*
    digitalRead(D5)

    With INPUT_PULLUP:
    - HIGH means open / not connected to ground
    - LOW means connected to ground
  */
  snapshot.digitalD5 = digitalRead(SAMPLE_DIGITAL_PIN);

  snapshot.uptimeMs = millis();
  snapshot.wifiRssi = WiFi.RSSI();
  snapshot.ip = WiFi.localIP();

  return snapshot;
}

String boardFullStateToJson(const BoardSnapshot &snapshot, unsigned long requestCount)
{
  /*
    Full JSON response.

    This includes LED state and general board status.
    It is kept for backwards compatibility at /api/state.
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
    Status-only JSON response.

    This intentionally excludes LED state.
    The webpage polls this periodically so that normal live updates do not
    constantly touch or refresh the LED control state.
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
    LED-only JSON response.

    This is returned by /api/led after a button press.
    It is small and fast, so LED commands feel more reliable.
  */
  String json = "{";
  json += "\"led_on\":";
  json += getLedState() ? "true" : "false";
  json += "}";

  return json;
}
