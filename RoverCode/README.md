# EEELunarRover Raw WiFi Production

This is the production dashboard build based on the working standalone raw-WiFi version.

## Main control behavior

Open `/dashboard`, click/tap the page once, then drive with the keyboard.

| Key | Command | Action |
|---|---|---|
| `W` / Arrow Up | `w` | Forward |
| `S` / Arrow Down | `s` | Reverse |
| `A` / Arrow Left | `a` | Strafe left |
| `D` / Arrow Right | `d` | Strafe right |
| `W+A` | `wa` | Forward-left |
| `W+D` | `wd` | Forward-right |
| `S+A` | `sa` | Reverse-left |
| `S+D` | `sd` | Reverse-right |
| `Q` | `q` | Rotate counterclockwise |
| `E` | `e` | Rotate clockwise |
| `Z` | speed toggle | Switch PWM between `110` and `255` |
| key release | recalculated command / stop | Releasing the final movement key sends `stop` immediately |

The old PWM slider has been removed. The dashboard now has exactly two speed values:

```text
110 = controlled / low speed
255 = full speed
```

If you press `Z` while moving, the dashboard immediately resends the current movement command with the new speed.

## Safety behavior

The rover sends `stop` when:

- the last movement key is released,
- the browser tab loses focus,
- the tab becomes hidden,
- the page is closed/refreshed,
- Emergency Stop is pressed,
- or the firmware motor watchdog stops receiving keepalive commands.

A 600 ms keepalive is sent while moving. The firmware watchdog is still the final backup.

## Upload steps

1. Open `EEELunarRover_RawWiFi_Production.ino` in Arduino IDE.
2. Install libraries:
   - `WiFi101_Generic`
   - `DFRobot_BMM150`
3. Edit `Secrets.h`:

```cpp
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
```

4. Confirm your physical pins in `AppConfig.h`.
5. Upload to the Adafruit Metro M0 Express.
6. Open Serial Monitor at `115200` for the printed IP address.
7. Test in this order:

```text
http://PRINTED_IP/api/ping
http://PRINTED_IP/dashboard
```

## Routes

| Route | Purpose |
|---|---|
| `/dashboard` | Production WASD dashboard |
| `/sensor` | Current rock classification JSON |
| `/debug` | Pin map, BMM150 ready state, simulated-mode flag, motor state |
| `/i2cscan` | I2C device scan |
| `/motor?cmd=w&speed=110` | Motor command endpoint |
| `/motor?cmd=stop` | Stop all motors |
| `/api/ping` | Connectivity test |
| `/api/status` | Uptime, RSSI, IP, request count, motor state |
| `/health` | Tiny health check |

## Hardware notes

- `PIN_ULTRASONIC` is intentionally left as `D1` to match your rover wiring.
- BMM150 retry-on-boot and `/i2cscan` are preserved from the standalone debug build.
- Raw `WiFiServer/WiFiClient` request handling is preserved because it was more stable on your hotspot than `WiFiWebServer`.
- Unknown, malformed, non-GET, or stalled requests are closed immediately.
- Every response ends with `client.stop()`.


## Motor command reliability fix

This build sends `/motor` commands using retained `XMLHttpRequest` objects instead of temporary `Image()` requests. The temporary image method could be garbage-collected or cancelled by the browser before the request reliably reached the WINC1500 server. The dashboard now shows an `ack ... HTTP 200` message when the rover confirms a motor command.

For hardware debugging, open this directly in the browser while watching Serial Monitor:

```text
http://PRINTED_IP/motor?cmd=w&speed=255
http://PRINTED_IP/motor?cmd=stop
```

A good firmware/network path prints `[MOTOR] w spd=255` and then `[MOTOR] stop spd=255` in Serial Monitor. If those logs appear but the motors do not move, the issue is motor power/driver wiring/enable pins rather than WiFi or dashboard code.
