# EEELunarRover Raw WiFi Integrated

This refactor keeps the stable annotated WINC1500 networking base and integrates the prototype rover logic into clean modules.

## What changed

- Replaced `WiFiWebServer` with the annotated raw `WiFiServer` / `WiFiClient` request lifecycle.
- Added `/dashboard`, `/sensor`, `/debug`, `/motor`, `/api/ping`, `/api/status`, `/health` routes.
- Preserved prototype logic for:
  - UART age parsing on `Serial1` at 600 bps,
  - IR Poisson log-likelihood classification,
  - ultrasonic present/absent classification,
  - BMM150 Z-axis polarity classification,
  - 2-of-3 / 3-of-3 rock fusion,
  - 4-wheel mecanum motor commands.
- Added a motor watchdog: if the dashboard stops sending keepalive commands, the rover stops after `MOTOR_COMMAND_TIMEOUT_MS`.

## Upload steps

1. Open `EEELunarRover_RawWiFi_Integrated_debugged.ino` in Arduino IDE.
2. Install libraries:
   - `WiFi101_Generic`
   - `DFRobot_BMM150`
3. Edit `Secrets.h`:

```cpp
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
```

4. Confirm pins in `AppConfig.h`, especially motor driver pins and `PIN_ULTRASONIC`.
5. Upload to Adafruit Metro M0 Express.
6. Open Serial Monitor at `115200` and use the printed dashboard URL.

## Routes

| Route | Purpose |
|---|---|
| `/dashboard` | Small diagnostic rover dashboard |
| `/sensor` | Current rock classification JSON |
| `/debug` | Pin map, BMM150 ready state, simulated-mode flag, motor state |
| `/motor?cmd=w&speed=180` | Motor command endpoint |
| `/motor?cmd=stop` | Stop all motors |
| `/api/ping` | Connectivity test |
| `/api/status` | Uptime, RSSI, IP, request count, motor state |
| `/health` | Tiny health check |

## Motor commands

| Command | Motion |
|---|---|
| `w` | forward |
| `s` | reverse |
| `a` | strafe left |
| `d` | strafe right |
| `wa` | forward-left |
| `wd` | forward-right |
| `sa` | reverse-left |
| `sd` | reverse-right |
| `q` | rotate counter-clockwise |
| `e` | rotate clockwise |
| `stop` | stop |

Unknown commands safely resolve to `stop`.

## Notes to verify on hardware

- `PIN_ULTRASONIC` is now `A1` by default because D0/D1 are reserved for `Serial1` on Metro M0. Rewire the 40 kHz detector output to A1 or choose another free input in `AppConfig.h`.
- The motor direction convention is preserved from the prototype: `LOW = forward`, `HIGH = reverse`. Flip this in `setMotor()` if your driver behaves oppositely.
- BMM150 setup now enables XYZ measurement with `setMeasurementXYZ()`. Polarity is preserved as `negative Z = DOWN`, `positive Z = UP`; flip the sign if the mounted sensor reports the opposite during the magnet test.
- Static EEERover IP is disabled by default. Set `EEEROVER_GROUP_NUMBER` in `AppConfig.h` if you need the old `192.168.0.<group+1>` behavior.
