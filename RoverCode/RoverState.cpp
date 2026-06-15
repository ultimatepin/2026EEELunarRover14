#include "RoverState.h"

/*
  RoverState.cpp
  ==============
  Integrated sensor, fusion, and motor logic from the prototype.

  Important design constraint:
    This file never handles WiFi clients directly. The raw HTTP router stays in
    WebRoutes.cpp so motor bursts cannot wedge the WINC1500 socket stack.
*/

namespace
{
  RockClassification currentRock;

  volatile unsigned long irPulseCount = 0;
  unsigned long irWindowStart = 0;
  bool irWindowActive = false;

  char uartBuffer[8];
  int uartBufferIdx = 0;
  bool uartReceiving = false;

  unsigned long lastSensorPoll = 0;
  unsigned long lastSerialPrint = 0;
  unsigned long lastSimUart = 0;

  bool bmm150Ready = false;
  unsigned long simSeed = 0;

  int motorSpeed = 180;
  char lastMotorCmd[8] = "stop";
  unsigned long lastMotorCommandMs = 0;

  DFRobot_BMM150_I2C bmm150(&Wire, I2C_ADDRESS_4);

  void copyText(char *dst, size_t dstSize, const char *src)
  {
    if (!dst || dstSize == 0)
    {
      return;
    }
    if (!src)
    {
      src = "";
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
  }

#if ROVER_SIMULATED_MODE
  uint32_t simRandom(uint32_t maxValue)
  {
    simSeed = simSeed * 1664525UL + 1013904223UL;
    return maxValue == 0 ? 0 : simSeed % maxValue;
  }
#endif

  // ========================== UART AGE DECODING ==========================

  void processUartByte(char c)
  {
    if (c == '#')
    {
      uartBuffer[0] = '#';
      uartBufferIdx = 1;
      uartReceiving = true;
      return;
    }

    if (!uartReceiving)
    {
      return;
    }

    if (uartBufferIdx < (int)sizeof(uartBuffer) - 1)
    {
      uartBuffer[uartBufferIdx++] = c;
    }

    if (uartBufferIdx >= 4)
    {
      uartBuffer[4] = '\0';

      bool valid = true;
      for (int i = 1; i < 4; i++)
      {
        if (uartBuffer[i] < '0' || uartBuffer[i] > '9')
        {
          valid = false;
          break;
        }
      }

      if (valid)
      {
        copyText(currentRock.age, sizeof(currentRock.age), uartBuffer);
        currentRock.ageValid = true;
        Serial.print(F("[UART] Age received: "));
        Serial.println(currentRock.age);
      }
      else
      {
        Serial.println(F("[UART] Invalid age string discarded"));
      }

      uartReceiving = false;
      uartBufferIdx = 0;
    }
  }

#if ROVER_SIMULATED_MODE
  void simulateUartReceive()
  {
    char simAge[5];
    simAge[0] = '#';
    simAge[1] = '1' + simRandom(4);
    simAge[2] = '0' + simRandom(10);
    simAge[3] = '0' + simRandom(10);
    simAge[4] = '\0';

    for (int i = 0; i < 4; i++)
    {
      processUartByte(simAge[i]);
    }
  }
#endif

  void pollUart()
  {
#if ROVER_SIMULATED_MODE
    return;
#else
    while (Serial1.available())
    {
      processUartByte((char)Serial1.read());
    }
#endif
  }

  // ========================== IR PULSE RATE ESTIMATION ==========================

  void irPulseISR()
  {
    irPulseCount++;
  }

#if ROVER_SIMULATED_MODE
  unsigned long simulateIrPulseCount(unsigned long windowMs)
  {
    const float trueLambda = (simRandom(2) == 0) ? LAMBDA_HIGH : LAMBDA_LOW;
    const float expectedCount = trueLambda * (windowMs / 1000.0f);
    const float u1 = (simRandom(10000) + 1) / 10000.0f;
    const float u2 = (simRandom(10000) + 1) / 10000.0f;
    const float noise = (u1 + u2 - 1.0f) * sqrt(expectedCount);
    long count = (long)(expectedCount + noise);
    return count < 0 ? 0 : (unsigned long)count;
  }
#endif

  SensorResult classifyIrRate()
  {
    SensorResult result;
    result.valid = false;
    result.classIndex = -1;
    result.confidence = 0.0f;

    unsigned long count = 0;
    float windowSec = 0.0f;

#if ROVER_SIMULATED_MODE
    windowSec = IR_WINDOW_MS / 1000.0f;
    count = simulateIrPulseCount(IR_WINDOW_MS);
#else
    const unsigned long now = millis();

    if (!irWindowActive)
    {
      noInterrupts();
      irPulseCount = 0;
      interrupts();
      irWindowStart = now;
      irWindowActive = true;
      Serial.println(F("[IR] Counting window started"));
      return result;
    }

    noInterrupts();
    count = irPulseCount;
    irPulseCount = 0;
    interrupts();

    const unsigned long elapsedMs = now - irWindowStart;
    irWindowStart = now;

    if (elapsedMs < 50)
    {
      Serial.println(F("[IR] Window too short; skipping"));
      return result;
    }

    windowSec = elapsedMs / 1000.0f;
#endif

    const float observedLambda = count / windowSec;
    const float logRatio = count * log(LAMBDA_HIGH / LAMBDA_LOW) - (LAMBDA_HIGH - LAMBDA_LOW) * windowSec;

    result.classIndex = (logRatio > 0.0f) ? 1 : 0;
    const float absLR = fabs(logRatio);
    result.confidence = absLR / (absLR + 5.0f);
    result.valid = true;

    currentRock.irDetails.count = count;
    currentRock.irDetails.windowSec = windowSec;
    currentRock.irDetails.observedLambda = observedLambda;
    currentRock.irDetails.logLikelihoodRatio = logRatio;

    Serial.print(F("[IR] Count="));
    Serial.print(count);
    Serial.print(F(" T="));
    Serial.print(windowSec, 3);
    Serial.print(F("s lambda="));
    Serial.print(observedLambda, 1);
    Serial.print(F(" LLR="));
    Serial.print(logRatio, 2);
    Serial.print(F(" -> "));
    Serial.println(result.classIndex ? F("HIGH") : F("LOW"));

    return result;
  }

  // ========================== ULTRASONIC DETECTION ==========================

#if ROVER_SIMULATED_MODE
  bool simulateUltrasonicPresent()
  {
    return simRandom(2) == 1;
  }
#endif

  SensorResult classifyUltrasonic()
  {
    SensorResult result;
#if ROVER_SIMULATED_MODE
    const bool present = simulateUltrasonicPresent();
#else
    const bool present = digitalRead(PIN_ULTRASONIC) == HIGH;
#endif
    result.valid = true;
    result.classIndex = present ? 1 : 0;
    result.confidence = 0.95f;

    Serial.print(F("[US] 40kHz "));
    Serial.println(present ? F("PRESENT") : F("ABSENT"));
    return result;
  }

  // ========================== MAGNETIC POLARITY ==========================

#if ROVER_SIMULATED_MODE
  float simulateBmm150Z()
  {
    return simRandom(2) == 0 ? -(200.0f + simRandom(150)) : (200.0f + simRandom(150));
  }
#endif

  SensorResult classifyMagnetic()
  {
    SensorResult result;
    result.valid = false;
    result.classIndex = -1;
    result.confidence = 0.0f;

    float z = 0.0f;

#if ROVER_SIMULATED_MODE
    z = simulateBmm150Z();
#else
    if (!bmm150Ready)
    {
      Serial.println(F("[MAG] BMM150 unavailable; skipping"));
      currentRock.magneticDetails.zUt = 0.0f;
      return result;
    }

    bmm150.setOperationMode(BMM150_POWERMODE_NORMAL);
    sBmm150MagData_t magData = bmm150.getGeomagneticData();
    z = magData.z;
#endif

    currentRock.magneticDetails.zUt = z;
    const float absZ = fabs(z);

    if (absZ < BMM150_DEADZONE_UT)
    {
      Serial.print(F("[MAG] Z="));
      Serial.print(z, 1);
      Serial.println(F(" uT -> UNCERTAIN"));
      return result;
    }

    result.valid = true;
    result.classIndex = (z < 0.0f) ? 1 : 0; // -Z = DOWN, +Z = UP.
    result.confidence = min(1.0f, absZ / BMM150_MAX_UT);

    Serial.print(F("[MAG] Z="));
    Serial.print(z, 1);
    Serial.print(F(" uT -> "));
    Serial.println(result.classIndex ? F("DOWN") : F("UP"));

    return result;
  }

  // ========================== MULTI-SENSOR FUSION ==========================

  const char *lookupRockType(int ir, int us, int mag)
  {
    const int code = (ir << 2) | (us << 1) | mag;
    switch (code)
    {
      case 7: return "Basaltoid"; // HIGH + PRESENT + DOWN
      case 1: return "Gravion";   // LOW  + ABSENT  + DOWN
      case 2: return "Regolix";   // LOW  + PRESENT + UP
      case 4: return "Lunarite";  // HIGH + ABSENT  + UP
      default: return "CONFLICT";
    }
  }

  void fuseAndClassify()
  {
    currentRock.ir = classifyIrRate();
    currentRock.ultrasonic = classifyUltrasonic();
    currentRock.magnetic = classifyMagnetic();

    const int validCount = (currentRock.ir.valid ? 1 : 0) +
                           (currentRock.ultrasonic.valid ? 1 : 0) +
                           (currentRock.magnetic.valid ? 1 : 0);

    if (validCount < 2)
    {
      copyText(currentRock.rockType, sizeof(currentRock.rockType), "UNKNOWN");
      currentRock.fusionConfidence = 0.0f;
      currentRock.fusionValid = false;
      Serial.println(F("[FUSION] Insufficient valid sensors"));
      return;
    }

    if (validCount == 3)
    {
      const char *fullResult = lookupRockType(currentRock.ir.classIndex,
                                              currentRock.ultrasonic.classIndex,
                                              currentRock.magnetic.classIndex);
      if (strcmp(fullResult, "CONFLICT") != 0)
      {
        copyText(currentRock.rockType, sizeof(currentRock.rockType), fullResult);
        currentRock.fusionConfidence = (currentRock.ir.confidence +
                                        currentRock.ultrasonic.confidence +
                                        currentRock.magnetic.confidence) / 3.0f;
        currentRock.fusionValid = true;
        Serial.print(F("[FUSION] All 3 sensors -> "));
        Serial.println(currentRock.rockType);
        return;
      }
      Serial.println(F("[FUSION] 3-sensor conflict; trying 2-sensor pairs"));
    }

    struct Pair
    {
      float totalConf;
      const char *result;
    };

    Pair bestPair;
    bestPair.totalConf = -1.0f;
    bestPair.result = "UNKNOWN";

    if (currentRock.ir.valid && currentRock.ultrasonic.valid)
    {
      int inferredMag;
      if (currentRock.ir.classIndex == 1 && currentRock.ultrasonic.classIndex == 1) inferredMag = 1;
      else if (currentRock.ir.classIndex == 0 && currentRock.ultrasonic.classIndex == 0) inferredMag = 1;
      else inferredMag = 0;

      const float conf = (currentRock.ir.confidence + currentRock.ultrasonic.confidence) / 2.0f;
      const char *res = lookupRockType(currentRock.ir.classIndex, currentRock.ultrasonic.classIndex, inferredMag);
      if (conf > bestPair.totalConf && strcmp(res, "CONFLICT") != 0)
      {
        bestPair.totalConf = conf;
        bestPair.result = res;
      }
    }

    if (currentRock.ir.valid && currentRock.magnetic.valid)
    {
      int inferredUs;
      if (currentRock.ir.classIndex == 1 && currentRock.magnetic.classIndex == 1) inferredUs = 1;
      else if (currentRock.ir.classIndex == 0 && currentRock.magnetic.classIndex == 1) inferredUs = 0;
      else if (currentRock.ir.classIndex == 0 && currentRock.magnetic.classIndex == 0) inferredUs = 1;
      else inferredUs = 0;

      const float conf = (currentRock.ir.confidence + currentRock.magnetic.confidence) / 2.0f;
      const char *res = lookupRockType(currentRock.ir.classIndex, inferredUs, currentRock.magnetic.classIndex);
      if (conf > bestPair.totalConf && strcmp(res, "CONFLICT") != 0)
      {
        bestPair.totalConf = conf;
        bestPair.result = res;
      }
    }

    if (currentRock.ultrasonic.valid && currentRock.magnetic.valid)
    {
      int inferredIr;
      if (currentRock.ultrasonic.classIndex == 1 && currentRock.magnetic.classIndex == 1) inferredIr = 1;
      else if (currentRock.ultrasonic.classIndex == 0 && currentRock.magnetic.classIndex == 1) inferredIr = 0;
      else if (currentRock.ultrasonic.classIndex == 1 && currentRock.magnetic.classIndex == 0) inferredIr = 0;
      else inferredIr = 1;

      const float conf = (currentRock.ultrasonic.confidence + currentRock.magnetic.confidence) / 2.0f;
      const char *res = lookupRockType(inferredIr, currentRock.ultrasonic.classIndex, currentRock.magnetic.classIndex);
      if (conf > bestPair.totalConf && strcmp(res, "CONFLICT") != 0)
      {
        bestPair.totalConf = conf;
        bestPair.result = res;
      }
    }

    copyText(currentRock.rockType, sizeof(currentRock.rockType), bestPair.result);
    currentRock.fusionConfidence = bestPair.totalConf;
    currentRock.fusionValid = bestPair.totalConf > 0.0f;

    Serial.print(F("[FUSION] Best 2-sensor pair -> "));
    Serial.println(currentRock.rockType);
  }

  // ========================== MECANUM MOTOR CONTROL ==========================

  void setMotor(uint8_t dirPin, uint8_t pwmPin, bool forward, int spd)
  {
    // Preserves prototype convention: LOW = forward, HIGH = reverse.
    digitalWrite(dirPin, forward ? LOW : HIGH);
    analogWrite(pwmPin, spd);
  }

  void setAllMotors(int fl, int fr, int rl, int rr, int spd)
  {
    setMotor(PIN_FL_DIR, PIN_FL_PWM, fl >= 0, fl == 0 ? 0 : spd);
    setMotor(PIN_FR_DIR, PIN_FR_PWM, fr >= 0, fr == 0 ? 0 : spd);
    setMotor(PIN_RL_DIR, PIN_RL_PWM, rl >= 0, rl == 0 ? 0 : spd);
    setMotor(PIN_RR_DIR, PIN_RR_PWM, rr >= 0, rr == 0 ? 0 : spd);
  }

  void motorForward()  { setAllMotors(+1, +1, +1, +1, motorSpeed); }
  void motorReverse()  { setAllMotors(-1, -1, -1, -1, motorSpeed); }
  void motorLeft()     { setAllMotors(-1, +1, +1, -1, motorSpeed); }
  void motorRight()    { setAllMotors(+1, -1, -1, +1, motorSpeed); }
  void motorFwdLeft()  { setAllMotors( 0, +1, +1,  0, motorSpeed); }
  void motorFwdRight() { setAllMotors(+1,  0,  0, +1, motorSpeed); }
  void motorRevLeft()  { setAllMotors(-1,  0,  0, -1, motorSpeed); }
  void motorRevRight() { setAllMotors( 0, -1, -1,  0, motorSpeed); }
  void motorRotCW()    { setAllMotors(+1, -1, +1, -1, motorSpeed); }
  void motorRotCCW()   { setAllMotors(-1, +1, -1, +1, motorSpeed); }
  void motorStop()     { setAllMotors( 0,  0,  0,  0, 0); }

  void motorsInit()
  {
    pinMode(PIN_FL_DIR, OUTPUT); pinMode(PIN_FL_PWM, OUTPUT);
    pinMode(PIN_FR_DIR, OUTPUT); pinMode(PIN_FR_PWM, OUTPUT);
    pinMode(PIN_RL_DIR, OUTPUT); pinMode(PIN_RL_PWM, OUTPUT);
    pinMode(PIN_RR_DIR, OUTPUT); pinMode(PIN_RR_PWM, OUTPUT);
    motorStop();
  }

  void enforceMotorWatchdog()
  {
    if (strcmp(lastMotorCmd, "stop") != 0 && millis() - lastMotorCommandMs > MOTOR_COMMAND_TIMEOUT_MS)
    {
      motorStop();
      copyText(lastMotorCmd, sizeof(lastMotorCmd), "stop");
      Serial.println(F("[MOTOR] Watchdog timeout -> stop"));
    }
  }

  const char *sensorLabel(const SensorResult &r, const char *zeroLabel, const char *oneLabel)
  {
    if (!r.valid)
    {
      return nullptr;
    }
    return r.classIndex ? oneLabel : zeroLabel;
  }

  bool pinEquals(uint8_t a, uint8_t b)
  {
    return a == b;
  }

  void printPinAudit()
  {
    Serial.println(F("[PIN] Active rover pin map:"));
    Serial.print(F("[PIN] IR interrupt = ")); Serial.println(PIN_IR_INTERRUPT);
    Serial.print(F("[PIN] Ultrasonic   = ")); Serial.println(PIN_ULTRASONIC);
    Serial.print(F("[PIN] FL DIR/PWM   = ")); Serial.print(PIN_FL_DIR); Serial.print('/'); Serial.println(PIN_FL_PWM);
    Serial.print(F("[PIN] FR DIR/PWM   = ")); Serial.print(PIN_FR_DIR); Serial.print('/'); Serial.println(PIN_FR_PWM);
    Serial.print(F("[PIN] RL DIR/PWM   = ")); Serial.print(PIN_RL_DIR); Serial.print('/'); Serial.println(PIN_RL_PWM);
    Serial.print(F("[PIN] RR DIR/PWM   = ")); Serial.print(PIN_RR_DIR); Serial.print('/'); Serial.println(PIN_RR_PWM);

    if (pinEquals(PIN_ULTRASONIC, 0) || pinEquals(PIN_ULTRASONIC, 1))
    {
      Serial.println(F("[PIN WARNING] Ultrasonic is on D0/D1, which conflicts with Serial1."));
    }
    if (pinEquals(PIN_IR_INTERRUPT, 4))
    {
      Serial.println(F("[PIN WARNING] D4 is not interrupt-capable on Metro M0."));
    }
    if (pinEquals(PIN_FL_DIR, 5) || pinEquals(PIN_FL_PWM, 5) ||
        pinEquals(PIN_FR_DIR, 5) || pinEquals(PIN_FR_PWM, 5) ||
        pinEquals(PIN_RL_DIR, 5) || pinEquals(PIN_RL_PWM, 5) ||
        pinEquals(PIN_RR_DIR, 5) || pinEquals(PIN_RR_PWM, 5) ||
        pinEquals(PIN_FL_DIR, 7) || pinEquals(PIN_FL_PWM, 7) ||
        pinEquals(PIN_FR_DIR, 7) || pinEquals(PIN_FR_PWM, 7) ||
        pinEquals(PIN_RL_DIR, 7) || pinEquals(PIN_RL_PWM, 7) ||
        pinEquals(PIN_RR_DIR, 7) || pinEquals(PIN_RR_PWM, 7) ||
        pinEquals(PIN_FL_DIR, 10) || pinEquals(PIN_FL_PWM, 10) ||
        pinEquals(PIN_FR_DIR, 10) || pinEquals(PIN_FR_PWM, 10) ||
        pinEquals(PIN_RL_DIR, 10) || pinEquals(PIN_RL_PWM, 10) ||
        pinEquals(PIN_RR_DIR, 10) || pinEquals(PIN_RR_PWM, 10))
    {
      Serial.println(F("[PIN WARNING] A motor pin is using D5, D7, or D10, reserved by WINC1500."));
    }
  }
}

void setupRoverHardware()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  printPinAudit();

  memset(&currentRock, 0, sizeof(currentRock));
  copyText(currentRock.age, sizeof(currentRock.age), "#---");
  copyText(currentRock.rockType, sizeof(currentRock.rockType), "UNKNOWN");
  currentRock.ageValid = false;
  currentRock.fusionValid = false;

#if !ROVER_SIMULATED_MODE
  Serial1.begin(600);
  pinMode(PIN_ULTRASONIC, INPUT);
  pinMode(PIN_IR_INTERRUPT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IR_INTERRUPT), irPulseISR, RISING);

  Wire.begin();
  const int BMM150_MAX_RETRIES = 3;
  for (int attempt = 1; attempt <= BMM150_MAX_RETRIES; attempt++)
  {
    if (bmm150.begin() == BMM150_OK)
    {
      bmm150.setOperationMode(BMM150_POWERMODE_NORMAL);
      bmm150.setPresetMode(BMM150_PRESETMODE_HIGHACCURACY);
      bmm150.setRate(BMM150_DATA_RATE_10HZ);
      bmm150.setMeasurementXYZ();
      bmm150Ready = true;
      Serial.println(F("[MAG] BMM150 initialised OK; XYZ measurement enabled"));
      break;
    }

    Serial.print(F("[MAG] BMM150 init attempt "));
    Serial.print(attempt);
    Serial.println(F(" failed; check SDA/SCL and CS->GND"));
    delay(500);
  }

  if (!bmm150Ready)
  {
    Serial.println(F("[MAG] BMM150 unavailable; using IR + ultrasonic when possible"));
  }
#endif

  motorsInit();
  lastMotorCommandMs = millis();
  simSeed = micros();

  Serial.println(F("[ROVER] Sensor/fusion/motor modules initialised"));
#if ROVER_SIMULATED_MODE
  Serial.println(F("[ROVER] ROVER_SIMULATED_MODE=1"));
#else
  Serial.println(F("[ROVER] ROVER_SIMULATED_MODE=0"));
#endif
}

void runRoverTasks()
{
  const unsigned long now = millis();

  pollUart();

#if ROVER_SIMULATED_MODE
  if (now - lastSimUart >= SIM_UART_INTERVAL_MS)
  {
    lastSimUart = now;
    simulateUartReceive();
  }
#endif

  if (now - lastSensorPoll >= SENSOR_POLL_INTERVAL_MS)
  {
    lastSensorPoll = now;
    fuseAndClassify();
  }

  enforceMotorWatchdog();

  if (now - lastSerialPrint >= SERIAL_PRINT_INTERVAL_MS)
  {
    lastSerialPrint = now;
    printFullRoverStatus();
  }
}

void applyMotorCommand(const char *cmd, int requestedSpeed, char *normalizedCmd, size_t normalizedSize, bool *knownCommand)
{
  if (requestedSpeed >= 0 && requestedSpeed <= 255)
  {
    motorSpeed = requestedSpeed;
  }

  char c[8];
  copyText(c, sizeof(c), cmd ? cmd : "");
  for (size_t i = 0; c[i]; i++)
  {
    c[i] = (char)tolower(c[i]);
  }

  bool known = true;

  if (c[0] == '\0' || strcmp(c, "stop") == 0)
  {
    motorStop();
    copyText(c, sizeof(c), "stop");
  }
  else if (strcmp(c, "w") == 0) motorForward();
  else if (strcmp(c, "s") == 0) motorReverse();
  else if (strcmp(c, "a") == 0) motorLeft();
  else if (strcmp(c, "d") == 0) motorRight();
  else if (strcmp(c, "wa") == 0 || strcmp(c, "aw") == 0) { motorFwdLeft(); copyText(c, sizeof(c), "wa"); }
  else if (strcmp(c, "wd") == 0 || strcmp(c, "dw") == 0) { motorFwdRight(); copyText(c, sizeof(c), "wd"); }
  else if (strcmp(c, "sa") == 0 || strcmp(c, "as") == 0) { motorRevLeft(); copyText(c, sizeof(c), "sa"); }
  else if (strcmp(c, "sd") == 0 || strcmp(c, "ds") == 0) { motorRevRight(); copyText(c, sizeof(c), "sd"); }
  else if (strcmp(c, "q") == 0) motorRotCCW();
  else if (strcmp(c, "e") == 0) motorRotCW();
  else
  {
    motorStop();
    copyText(c, sizeof(c), "stop");
    known = false;
  }

  copyText(lastMotorCmd, sizeof(lastMotorCmd), c);
  lastMotorCommandMs = millis();
  copyText(normalizedCmd, normalizedSize, c);

  if (knownCommand)
  {
    *knownCommand = known;
  }

  Serial.print(F("[MOTOR] "));
  Serial.print(c);
  Serial.print(F(" spd="));
  Serial.print(motorSpeed);
  if (!known)
  {
    Serial.print(F(" unknown -> stopped"));
  }
  Serial.println();
}

String roverSensorJson()
{
  String json = "{";

  json += "\"age\":";
  json += currentRock.ageValid ? ("\"" + String(currentRock.age) + "\"") : "null";

  json += ",\"ir\":{";
  json += "\"class\":";
  json += currentRock.ir.valid ? String(currentRock.ir.classIndex) : "null";
  json += ",\"conf\":";
  json += currentRock.ir.valid ? String(currentRock.ir.confidence, 3) : "null";
  json += ",\"label\":";
  const char *irLabel = sensorLabel(currentRock.ir, "LOW", "HIGH");
  json += irLabel ? ("\"" + String(irLabel) + "\"") : "null";
  json += ",\"count\":";
  json += currentRock.ir.valid ? String(currentRock.irDetails.count) : "null";
  json += ",\"lambda\":";
  json += currentRock.ir.valid ? String(currentRock.irDetails.observedLambda, 1) : "null";
  json += ",\"llr\":";
  json += currentRock.ir.valid ? String(currentRock.irDetails.logLikelihoodRatio, 3) : "null";
  json += "}";

  json += ",\"ultrasonic\":{";
  json += "\"class\":";
  json += currentRock.ultrasonic.valid ? String(currentRock.ultrasonic.classIndex) : "null";
  json += ",\"conf\":";
  json += currentRock.ultrasonic.valid ? String(currentRock.ultrasonic.confidence, 3) : "null";
  json += ",\"label\":";
  const char *usLabel = sensorLabel(currentRock.ultrasonic, "ABSENT", "PRESENT");
  json += usLabel ? ("\"" + String(usLabel) + "\"") : "null";
  json += "}";

  json += ",\"magnetic\":{";
  json += "\"class\":";
  json += currentRock.magnetic.valid ? String(currentRock.magnetic.classIndex) : "null";
  json += ",\"conf\":";
  json += currentRock.magnetic.valid ? String(currentRock.magnetic.confidence, 3) : "null";
  json += ",\"label\":";
  const char *magLabel = sensorLabel(currentRock.magnetic, "UP", "DOWN");
  json += magLabel ? ("\"" + String(magLabel) + "\"") : "null";
  json += ",\"z\":";
  json += currentRock.magnetic.valid ? String(currentRock.magneticDetails.zUt, 1) : "null";
  json += "}";

  json += ",\"rockType\":";
  json += currentRock.fusionValid ? ("\"" + String(currentRock.rockType) + "\"") : "null";
  json += ",\"fusionConf\":";
  json += currentRock.fusionValid ? String(currentRock.fusionConfidence, 3) : "null";
  json += "}";

  return json;
}

String roverMotorJson()
{
  String json = "{";
  json += "\"cmd\":\"";
  json += lastMotorCmd;
  json += "\",\"speed\":";
  json += motorSpeed;
  json += "}";
  return json;
}

String roverDebugJson()
{
  String json = "{";
  json += "\"simulated\":";
#if ROVER_SIMULATED_MODE
  json += "true";
#else
  json += "false";
#endif
  json += ",\"bmm150_ready\":";
  json += bmm150Ready ? "true" : "false";
  json += ",\"pins\":{";
  json += "\"ir_interrupt\":"; json += PIN_IR_INTERRUPT;
  json += ",\"ultrasonic\":"; json += PIN_ULTRASONIC;
  json += ",\"fl_dir\":"; json += PIN_FL_DIR;
  json += ",\"fl_pwm\":"; json += PIN_FL_PWM;
  json += ",\"fr_dir\":"; json += PIN_FR_DIR;
  json += ",\"fr_pwm\":"; json += PIN_FR_PWM;
  json += ",\"rl_dir\":"; json += PIN_RL_DIR;
  json += ",\"rl_pwm\":"; json += PIN_RL_PWM;
  json += ",\"rr_dir\":"; json += PIN_RR_DIR;
  json += ",\"rr_pwm\":"; json += PIN_RR_PWM;
  json += "},\"motor\":";
  json += roverMotorJson();
  json += "}";
  return json;
}

void printFullRoverStatus()
{
  Serial.println(F("========== ROCK CLASSIFICATION STATUS =========="));

  Serial.print(F("  Age:  "));
  if (currentRock.ageValid)
  {
    Serial.print(currentRock.age);
    if (currentRock.age[0] == '#')
    {
      Serial.print(F(" -> "));
      Serial.print(currentRock.age[1]);
      Serial.print('.');
      Serial.print(currentRock.age[2]);
      Serial.print(currentRock.age[3]);
      Serial.print(F(" billion years"));
    }
  }
  else
  {
    Serial.print(F("not received"));
  }
  Serial.println();

  Serial.print(F("  IR:   "));
  if (currentRock.ir.valid)
  {
    Serial.print(currentRock.ir.classIndex ? F("HIGH (547)") : F("LOW (312)"));
    Serial.print(F(" conf="));
    Serial.print(currentRock.ir.confidence, 2);
  }
  else Serial.print(F("invalid"));
  Serial.println();

  Serial.print(F("  US:   "));
  if (currentRock.ultrasonic.valid)
  {
    Serial.print(currentRock.ultrasonic.classIndex ? F("PRESENT") : F("ABSENT"));
    Serial.print(F(" conf="));
    Serial.print(currentRock.ultrasonic.confidence, 2);
  }
  else Serial.print(F("invalid"));
  Serial.println();

  Serial.print(F("  Mag:  "));
  if (currentRock.magnetic.valid)
  {
    Serial.print(currentRock.magnetic.classIndex ? F("DOWN") : F("UP"));
    Serial.print(F(" conf="));
    Serial.print(currentRock.magnetic.confidence, 2);
  }
  else Serial.print(F("invalid"));
  Serial.println();

  Serial.print(F("  Type: "));
  if (currentRock.fusionValid)
  {
    Serial.print(currentRock.rockType);
    Serial.print(F(" conf="));
    Serial.print(currentRock.fusionConfidence, 2);
  }
  else Serial.print(F("UNKNOWN"));
  Serial.println();
  Serial.println(F("================================================"));
}
