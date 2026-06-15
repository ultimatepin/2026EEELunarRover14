#pragma once

#include "AppConfig.h"

/*
  BoardState.h
  ============

  This file declares the hardware-state interface.

  Django analogy:
  - BoardSnapshot is like a small context dict/object collected before rendering
    a response.
  - BoardState.cpp is like the model/service layer that knows how to read the
    physical board.
  - WebRoutes.cpp should not directly contain pin-reading details; it calls
    readBoardSnapshot() instead.
*/

/*
  BoardSnapshot
  -------------

  A struct groups related values together.

  Python/Django comparison:
    snapshot = {
      "ledOn": True,
      "analogA0": 417,
      ...
    }

  C++ version:
    BoardSnapshot snapshot;
    snapshot.ledOn = true;

  Field meanings:
    ledOn:      last LED state written by setLedState().
    analogA0:   raw analogRead(A0) value.
    digitalD5:  digitalRead(D5), usually HIGH or LOW.
    uptimeMs:   milliseconds since board boot.
    wifiRssi:   WiFi signal strength in dBm; less negative is stronger.
    ip:         current local IP address.
*/
struct BoardSnapshot
{
  bool ledOn;
  int analogA0;
  int digitalD5;
  unsigned long uptimeMs;
  long wifiRssi;
  IPAddress ip;
};

/*
  Initializes pin modes.
  Call once from setup().
*/
void setupBoardPins();

/*
  LED state helpers.
  The board stores LED state manually because Arduino does not provide a clean,
  universal "what value did I last write to this output pin?" API.
*/
bool getLedState();
void setLedState(bool on);

/*
  Reads all current board values into one snapshot.
  Web routes call this before generating JSON.
*/
BoardSnapshot readBoardSnapshot();

/*
  JSON rendering helpers.

  These return Arduino String objects containing JSON text.
  On small boards, avoid huge JSON. These responses are intentionally compact.
*/
String boardFullStateToJson(const BoardSnapshot &snapshot, unsigned long requestCount);
String boardStatusToJson(const BoardSnapshot &snapshot, unsigned long requestCount);
String boardLedToJson();
