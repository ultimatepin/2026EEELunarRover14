# Metro M0 + WINC1500 Web Base v3 Annotated

Arduino-ready annotated version of the fixed final v3 project.

This build is designed for a fragile phone-hotspot/browser environment:

- raw `WiFiServer` / `WiFiClient`, not `WiFiWebServer`
- strict `GET` route allowlist
- tiny safe `/` route
- dashboard at `/dashboard`
- first-line-only HTTP parsing
- short timeout for stalled sockets
- every request ends with `client.stop()`

## Open in Arduino IDE

1. Open `MetroM0_WINC1500_WebBase_v3_annotated.ino`.
2. Install `WiFi101_Generic` if needed.
3. Edit `Secrets.h`:

```cpp
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
```

4. Select the Adafruit Metro M0 board.
5. Upload.
6. Open Serial Monitor at `115200`.

## Test order

Use the printed IP address from Serial Monitor.

```text
http://PRINTED_IP/api/ping
http://PRINTED_IP/
http://PRINTED_IP/dashboard
```

Expected `/api/ping` response:

```text
pong
```

Expected useful routes:

| Route | Purpose |
|---|---|
| `/` | Tiny safe text response |
| `/dashboard` | Small browser dashboard |
| `/api/ping` | Minimal connectivity test |
| `/api/status` | JSON status without LED state |
| `/api/state` | Full JSON status including LED state |
| `/api/led?value=1` | Turn LED on |
| `/api/led?value=0` | Turn LED off |
| `/health` | Tiny health check |
| `/favicon.ico` | No-content response for browser noise |

## Mental model for Django users

| Django idea | This Arduino project |
|---|---|
| `settings.py` | `AppConfig.h` |
| `.env` / local secrets | `Secrets.h` |
| `urls.py` | route allowlist + dispatch in `WebRoutes.cpp` |
| view function | `handlePing()`, `handleStatus()`, `handleLed()`, etc. |
| `JsonResponse` | `sendString(..., "application/json", json)` |
| template response | static `DASHBOARD_HTML` string |
| request object | raw first line like `GET /api/ping HTTP/1.1` |
| server startup | `setup()` |
| request/event loop | `loop()` |

## Working principle

The board cannot afford to behave like a full web framework. The WINC1500 has limited sockets, and your hotspot/browser combination can open partial or unexpected connections.

So the server follows this strict flow:

```text
client = server.available()
if no client: return
read one request line with timeout
parse method and target
if not GET: close
if path not allowlisted: close
dispatch known route
send headers + body
client.stop()
return to loop
```

The most important line is:

```cpp
client.stop();
```

That releases the socket after every request.

## Common edits

### Change polling interval

In `WebRoutes.cpp`, dashboard JavaScript contains:

```js
setInterval(S,5000)
```

`5000` means 5000 ms = 5 seconds. For a hotspot, keep this conservative.

### Add a new JSON field

1. Add a field to `BoardSnapshot` in `BoardState.h`.
2. Fill it in `readBoardSnapshot()` in `BoardState.cpp`.
3. Add it to `boardStatusToJson()` or `boardFullStateToJson()`.

### Add a new route

In `WebRoutes.cpp`:

1. Add it to `isExpectedGetTarget()`.
2. Add a handler function, e.g. `handleTemperature()`.
3. Add a dispatch branch in `handleHttpClient()`.

Example pattern:

```cpp
else if (targetMatchesPath(target, "/api/example"))
{
  handleExample(client);
}
```

### Change pins

Edit `AppConfig.h`:

```cpp
const uint8_t SAMPLE_DIGITAL_PIN = 5;
const uint8_t SAMPLE_ANALOG_PIN = A0;
```

Avoid pins used by the WINC1500 shield/SPI.

## File guide

| File | What it does |
|---|---|
| `MetroM0_WINC1500_WebBase_v3_annotated.ino` | Program entry point: setup and loop |
| `AppConfig.h` | Constants, pins, timeouts, library imports |
| `Secrets.h` | WiFi SSID/password placeholders |
| `Network.cpp/.h` | WiFi connect/reconnect and IP formatting |
| `BoardState.cpp/.h` | Hardware reads, LED state, JSON formatting |
| `WebRoutes.cpp/.h` | Raw HTTP parser, route allowlist, responses, dashboard |

## Why `/` is tiny and dashboard is `/dashboard`

Browsers often request `/` automatically. In previous versions, `/` served the full dashboard and sometimes froze the board before the socket closed.

Now `/` is deliberately tiny, and the dashboard lives at `/dashboard`. This makes accidental root requests harmless.
