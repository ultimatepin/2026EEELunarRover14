#pragma once

#include "AppConfig.h"

/*
  BoardSnapshot

  A small struct is used like a simple Django context object:
  collect values from hardware first, then render/send them later.

  This keeps "reading hardware" separate from "formatting HTTP response".
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

void setupBoardPins();

bool getLedState();
void setLedState(bool on);

BoardSnapshot readBoardSnapshot();

String boardFullStateToJson(const BoardSnapshot &snapshot, unsigned long requestCount);
String boardStatusToJson(const BoardSnapshot &snapshot, unsigned long requestCount);
String boardLedToJson();
