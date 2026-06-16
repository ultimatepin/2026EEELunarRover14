#pragma once

#include "AppConfig.h"

/*
  RoverState.h
  ============
  Hardware-facing rover interface. WebRoutes.cpp asks this module for JSON or
  sends a normalized motor command; it does not know about pins or sensors.
*/

struct SensorResult
{
  bool valid;
  int classIndex;       // IR: 0 low / 1 high; US: 0 absent / 1 present; MAG: 0 up / 1 down.
  float confidence;
};

struct IrDetails
{
  unsigned long count;
  float windowSec;
  float observedLambda;
  float logLikelihoodRatio;
};

struct MagneticDetails
{
  float zUt;
};

struct RockClassification
{
  char age[5];
  bool ageValid;

  SensorResult ir;
  SensorResult ultrasonic;
  SensorResult magnetic;

  IrDetails irDetails;
  MagneticDetails magneticDetails;

  char rockType[12];
  float fusionConfidence;
  bool fusionValid;
};

void setupRoverHardware();
void runRoverTasks();

void applyMotorCommand(const char *cmd, int requestedSpeed, char *normalizedCmd, size_t normalizedSize, bool *knownCommand);
String roverSensorJson();
String roverMotorJson();
String roverRadioJson();
String roverDebugJson();
String roverI2cScanJson();
void printFullRoverStatus();
