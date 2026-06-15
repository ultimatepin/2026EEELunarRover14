# Detailed Walkthrough

## 1. Startup sequence

The `.ino` file runs `setup()` once:

```text
Serial.begin()
setupBoardPins()
connectToWiFiBlocking()
server.begin()
```

Then `loop()` repeats forever:

```text
handleHttpClient()
maintainWiFi()
```

This is the core Arduino pattern: one-time setup, then an infinite loop.

## 2. Request lifecycle

Browser sends:

```text
GET /api/ping HTTP/1.1
Host: 172.20.10.3
...
```

The board only reads the first line:

```text
GET /api/ping HTTP/1.1
```

Then `parseRequestLine()` extracts:

```text
method = GET
target = /api/ping
```

Then it checks:

```text
method must be GET
target must be allowlisted
```

Then it dispatches:

```text
/api/ping -> handlePing()
```

Then it sends:

```text
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 5
Connection: close
Cache-Control: no-store

pong
```

Then it closes:

```cpp
client.stop();
```

## 3. Parameters and constants

### `HTTP_PORT`

Defined in `AppConfig.h`.

```cpp
const uint16_t HTTP_PORT = 80;
```

Port 80 means normal browser URLs do not need a port suffix.

### `HTTP_CLIENT_TIMEOUT_MS`

```cpp
const unsigned long HTTP_CLIENT_TIMEOUT_MS = 350;
```

The board waits only 350 ms for the first request line. This prevents stalled hotspot/browser sockets from blocking future requests.

### `HTTP_REQUEST_LINE_MAX`

```cpp
const size_t HTTP_REQUEST_LINE_MAX = 96;
```

Maximum storage for the first request line.

### `HTTP_TARGET_MAX`

```cpp
const size_t HTTP_TARGET_MAX = 64;
```

Maximum storage for the route/target, such as `/api/led?value=1`.

### `WIFI_MAINTAIN_INTERVAL_MS`

```cpp
const unsigned long WIFI_MAINTAIN_INTERVAL_MS = 10000;
```

Checks WiFi health every 10 seconds.

## 4. Route usage

### `/api/ping`

Use first after reset.

```text
http://PRINTED_IP/api/ping
```

Response:

```text
pong
```

### `/api/status`

Returns changing board data.

```json
{
  "analog_a0": 417,
  "digital_d5": 1,
  "uptime_ms": 15342,
  "wifi_rssi": -45,
  "ip": "172.20.10.3",
  "request_count": 27
}
```

### `/api/state`

Same idea as `/api/status`, but includes LED state.

### `/api/led?value=1`

Turns the LED on.

Accepted on values:

```text
1
true
on
```

### `/api/led?value=0`

Turns the LED off.

Accepted off values:

```text
0
false
off
```

## 5. How to extend it safely

### Add sensor data

In `BoardState.h`:

```cpp
int newSensorValue;
```

In `readBoardSnapshot()`:

```cpp
snapshot.newSensorValue = analogRead(A1);
```

In `boardStatusToJson()`:

```cpp
json += ",\"new_sensor_value\":";
json += snapshot.newSensorValue;
```

### Add a new endpoint

In `WebRoutes.cpp`, create:

```cpp
void handleExample(WiFiClient &client)
{
  requestCount++;
  sendText(client, 200, "OK", "text/plain", "example\n");
}
```

Add allowlist entry:

```cpp
targetMatchesPath(target, "/api/example")
```

Add dispatch:

```cpp
else if (targetMatchesPath(target, "/api/example"))
{
  handleExample(client);
}
```

Keep responses small and always let `handleHttpClient()` reach `client.stop()`.

## 6. Debugging Serial logs

Good request:

```text
HTTP client connected
HTTP request line: GET /api/ping HTTP/1.1
HTTP method: [GET]
HTTP route: [/api/ping]
handlePing ms: 5
HTTP client closed ms: 8
```

Unknown path:

```text
HTTP route: [/random]
Skipping unexpected request: unknown path | [/random]
```

Stalled socket:

```text
HTTP client connected
Empty/stalled client; closing.
```

The key thing to watch for is whether `HTTP client closed ms:` appears after a valid request. If it does, the socket was released.
