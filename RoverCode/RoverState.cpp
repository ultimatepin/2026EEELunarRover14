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

  // Radio/UART diagnostics. These are exposed in /radio, /debug, and /sensor.
  char plainUartDigits[4];
  int plainUartDigitIdx = 0;
  unsigned long radioByteCount = 0;
  unsigned long radioValidPackets = 0;
  unsigned long radioInvalidPackets = 0;
  unsigned long radioLastByteMs = 0;
  unsigned long radioLastValidPacketMs = 0;
  uint8_t radioLastByte = 0;
  char radioLastPacket[5] = "#---";

  unsigned long lastSensorPoll = 0;
  unsigned long lastSerialPrint = 0;
  unsigned long lastSimUart = 0;

  bool bmm150Ready = false;
  unsigned long bmm150InitAttempts = 0;
  unsigned long lastBmm150RetryMs = 0;
  unsigned long simSeed = 0;

  int motorSpeed = 180;
  char lastMotorCmd[8] = "stop";
  unsigned long lastMotorCommandMs = 0;

  DFRobot_BMM150_I2C bmm150(&Wire, I2C_ADDRESS_4);


  bool serialDebugEnabled()
  {
    /*
      On the Metro M0, Serial is USB CDC. When the rover is powered from a
      battery or wall adapter without a computer attached, Serial may not be
      open. Large Serial logs are useful on the bench, but should not be part
      of the rover's runtime behaviour when it is standalone.
    */
    return Serial;
  }

#if !ROVER_SIMULATED_MODE
  bool tryInitBmm150(bool verbose)
  {
    bmm150InitAttempts++;

    if (bmm150.begin() == BMM150_OK)
    {
      bmm150.setOperationMode(BMM150_POWERMODE_NORMAL);
      bmm150.setPresetMode(BMM150_PRESETMODE_HIGHACCURACY);
      bmm150.setRate(BMM150_DATA_RATE_10HZ);
      bmm150.setMeasurementXYZ();
      bmm150Ready = true;

      if (verbose && serialDebugEnabled())
      {
        Serial.print(F("[MAG] BMM150 initialised OK after attempt "));
        Serial.println(bmm150InitAttempts);
      }
      return true;
    }

    bmm150Ready = false;
    if (verbose && serialDebugEnabled())
    {
      Serial.print(F("[MAG] BMM150 init attempt "));
      Serial.print(bmm150InitAttempts);
      Serial.println(F(" failed; will retry in background"));
    }
    return false;
  }

  void retryBmm150IfNeeded()
  {
    if (bmm150Ready)
    {
      return;
    }

    const unsigned long now = millis();
    if (now - lastBmm150RetryMs < BMM150_RETRY_INTERVAL_MS)
    {
      return;
    }

    lastBmm150RetryMs = now;
    tryInitBmm150(true);
  }
#endif

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

  // ========================== UART / RADIO AGE DECODING ==========================

  bool isDigitChar(char c)
  {
    return c >= '0' && c <= '9';
  }

  void resetHashPacket()
  {
    uartReceiving = false;
    uartBufferIdx = 0;
    uartBuffer[0] = '\0';
  }

  void resetPlainPacket()
  {
    plainUartDigitIdx = 0;
    plainUartDigits[0] = '\0';
  }

  void acceptRadioPacket(const char *packet)
  {
    copyText(currentRock.age, sizeof(currentRock.age), packet);
    copyText(radioLastPacket, sizeof(radioLastPacket), packet);
    currentRock.ageValid = true;
    radioValidPackets++;
    radioLastValidPacketMs = millis();

    if (serialDebugEnabled())
    {
      Serial.print(F("[RADIO] Age received: "));
      Serial.println(currentRock.age);
    }
  }

  void rejectRadioPacket(const __FlashStringHelper *reason)
  {
    radioInvalidPackets++;
    if (serialDebugEnabled())
    {
      Serial.print(F("[RADIO] Invalid/partial packet discarded: "));
      Serial.println(reason);
    }
  }

  void acceptPlainDigitPacketIfComplete()
  {
    if (!RADIO_ACCEPT_PLAIN_3_DIGIT_AGE || plainUartDigitIdx < 3)
    {
      return;
    }

    char normalized[5];
    normalized[0] = '#';
    normalized[1] = plainUartDigits[0];
    normalized[2] = plainUartDigits[1];
    normalized[3] = plainUartDigits[2];
    normalized[4] = '\0';
    acceptRadioPacket(normalized);
    resetPlainPacket();
  }

  void processUartByte(char c)
  {
    // Treat whitespace as a separator. If it arrives mid-packet, the packet was incomplete.
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
    {
      if (uartReceiving && uartBufferIdx > 1)
      {
        rejectRadioPacket(F("separator before #ddd completed"));
      }
      if (plainUartDigitIdx > 0)
      {
        rejectRadioPacket(F("separator before plain ddd completed"));
      }
      resetHashPacket();
      resetPlainPacket();
      return;
    }

    if (c == '#')
    {
      resetPlainPacket();
      uartBuffer[0] = '#';
      uartBufferIdx = 1;
      uartReceiving = true;
      return;
    }

    if (uartReceiving)
    {
      if (!isDigitChar(c))
      {
        rejectRadioPacket(F("non-digit after #"));
        resetHashPacket();
        return;
      }

      if (uartBufferIdx < 4)
      {
        uartBuffer[uartBufferIdx++] = c;
      }

      if (uartBufferIdx >= 4)
      {
        uartBuffer[4] = '\0';
        acceptRadioPacket(uartBuffer);
        resetHashPacket();
      }
      return;
    }

    // Extra tolerance: if the transmitter sends 123 instead of #123, accept it.
    if (RADIO_ACCEPT_PLAIN_3_DIGIT_AGE && isDigitChar(c))
    {
      if (plainUartDigitIdx < 3)
      {
        plainUartDigits[plainUartDigitIdx++] = c;
        plainUartDigits[plainUartDigitIdx] = '\0';
      }
      acceptPlainDigitPacketIfComplete();
      return;
    }

    resetPlainPacket();
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
      const uint8_t raw = (uint8_t)Serial1.read();
      const char c = (char)raw;

      radioByteCount++;
      radioLastByte = raw;
      radioLastByteMs = millis();

      if (RADIO_RAW_SERIAL_DEBUG && serialDebugEnabled())
      {
        Serial.print(F("[RADIO RAW] dec="));
        Serial.print(raw);
        Serial.print(F(" hex=0x"));
        if (raw < 16) Serial.print('0');
        Serial.print(raw, HEX);
        Serial.print(F(" char='"));
        if (raw >= 32 && raw <= 126) Serial.print((char)raw);
        else Serial.print('.');
        Serial.println(F("'"));
      }

      processUartByte(c);
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
    Serial.print(F("[PIN] Radio RX     = D")); Serial.print(PIN_RADIO_RX); Serial.print(F(" @ ")); Serial.print(RADIO_BAUD); Serial.println(F(" baud"));
    Serial.print(F("[PIN] Ultrasonic   = ")); Serial.println(PIN_ULTRASONIC);
    Serial.print(F("[PIN] FL DIR/PWM   = ")); Serial.print(PIN_FL_DIR); Serial.print('/'); Serial.println(PIN_FL_PWM);
    Serial.print(F("[PIN] FR DIR/PWM   = ")); Serial.print(PIN_FR_DIR); Serial.print('/'); Serial.println(PIN_FR_PWM);
    Serial.print(F("[PIN] RL DIR/PWM   = ")); Serial.print(PIN_RL_DIR); Serial.print('/'); Serial.println(PIN_RL_PWM);
    Serial.print(F("[PIN] RR DIR/PWM   = ")); Serial.print(PIN_RR_DIR); Serial.print('/'); Serial.println(PIN_RR_PWM);

    if (pinEquals(PIN_ULTRASONIC, 0) || pinEquals(PIN_ULTRASONIC, 1))
    {
      Serial.println(F("[PIN NOTE] Ultrasonic is intentionally on D0/D1 in this build."));
      Serial.println(F("[PIN NOTE] If readings fail, test Serial1/D1 interaction first."));
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
  Serial1.begin(RADIO_BAUD);
  if (serialDebugEnabled())
  {
    Serial.print(F("[RADIO] Serial1 RX active on D"));
    Serial.print(PIN_RADIO_RX);
    Serial.print(F(" at "));
    Serial.print(RADIO_BAUD);
    Serial.println(F(" baud"));
    Serial.println(F("[RADIO] Accepting #ddd and plain ddd age packets"));
  }
  pinMode(PIN_ULTRASONIC, INPUT);
  pinMode(PIN_IR_INTERRUPT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IR_INTERRUPT), irPulseISR, RISING);

  Wire.begin();

  /*
    Standalone-power fix:
    The magnetometer may not be fully awake at the exact moment the Metro M0
    boots from a battery/wall adapter. The older code tried only at startup and
    then permanently marked bmm150_ready=false. Now we do one fast attempt here
    and keep retrying in runRoverTasks() without blocking HTTP.
  */
  lastBmm150RetryMs = millis();
  tryInitBmm150(true);

  if (!bmm150Ready && serialDebugEnabled())
  {
    Serial.println(F("[MAG] BMM150 unavailable at boot; /debug and /i2cscan can diagnose it"));
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

#if !ROVER_SIMULATED_MODE
  retryBmm150IfNeeded();
#endif

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
  json += ",\"radio\":";
  json += roverRadioJson();
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

String roverRadioJson()
{
  String json = "{";
  json += "\"rx_pin\":";
  json += PIN_RADIO_RX;
  json += ",\"baud\":";
  json += RADIO_BAUD;
  json += ",\"accept_plain_ddd\":";
  json += RADIO_ACCEPT_PLAIN_3_DIGIT_AGE ? "true" : "false";
  json += ",\"raw_serial_debug\":";
  json += RADIO_RAW_SERIAL_DEBUG ? "true" : "false";
  json += ",\"bytes\":";
  json += radioByteCount;
  json += ",\"valid_packets\":";
  json += radioValidPackets;
  json += ",\"invalid_packets\":";
  json += radioInvalidPackets;
  json += ",\"last_byte_age_ms\":";
  json += radioByteCount > 0 ? String(millis() - radioLastByteMs) : "null";
  json += ",\"last_valid_packet_age_ms\":";
  json += radioValidPackets > 0 ? String(millis() - radioLastValidPacketMs) : "null";
  json += ",\"last_byte_dec\":";
  json += radioByteCount > 0 ? String(radioLastByte) : "null";
  json += ",\"last_byte_hex\":";
  if (radioByteCount > 0)
  {
    json += "\"0x";
    if (radioLastByte < 16) json += '0';
    json += String(radioLastByte, HEX);
    json += "\"";
  }
  else
  {
    json += "null";
  }
  json += ",\"last_packet\":\"";
  json += radioLastPacket;
  json += "\"";
  json += ",\"age\":";
  json += currentRock.ageValid ? ("\"" + String(currentRock.age) + "\"") : "null";
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
  json += ",\"bmm150_init_attempts\":";
  json += bmm150InitAttempts;
  json += ",\"uptime_ms\":";
  json += millis();
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
  json += ",\"radio\":";
  json += roverRadioJson();
  json += "}";
  return json;
}


String roverI2cScanJson()
{
  String json = "{\"devices\":[";
  bool first = true;

#if ROVER_SIMULATED_MODE
  json += "\"simulated\"";
#else
  for (uint8_t address = 1; address < 127; address++)
  {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0)
    {
      if (!first) json += ',';
      first = false;
      json += "\"0x";
      if (address < 16) json += '0';
      json += String(address, HEX);
      json += "\"";
    }
  }
#endif

  json += "],\"bmm150_ready\":";
  json += bmm150Ready ? "true" : "false";
  json += ",\"bmm150_init_attempts\":";
  json += bmm150InitAttempts;
  json += "}";
  return json;
}

void printFullRoverStatus()
{
  if (!serialDebugEnabled())
  {
    return;
  }

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
