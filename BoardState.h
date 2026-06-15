#pragma once

#include "AppConfig.h"

/*
  BoardSnapshot

  A small struct is used like a simple Django context object:
  collect values from hardware first, then render/send them later.

  This keeps "reading hardware" separate from "formatting HTTP response".
*/
// struct is like a python class instance, all fields are public
// Except for can't add new fields, it's like a data holder
// It becomes a data type which is used to define readBoardSnapshot()
// Needs modification to include all the pins
struct BoardSnapshot
{
  bool ledOn;
  int analogA0;
  int digitalD5;
  // How long the board ran since reset
  unsigned long uptimeMs;
  // Received signal strength indicator, closer to 0 the better (<50 good)
  long wifiRssi;
  IPAddress ip;
}; 

void setupBoardPins();

bool getLedState();
void setLedState(bool on);

BoardSnapshot readBoardSnapshot();
String boardSnapshotToJson(const BoardSnapshot &snapshot, unsigned long requestCount);
