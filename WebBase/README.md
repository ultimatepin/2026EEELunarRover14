# Metro M0 + WINC1500 WiFi Raw API Server v3.5

This version is designed for unstable phone-hotspot testing.

Changes from v3.4:

- Keeps `/` tiny instead of sending the full HTML page.
  - v3.4 could block while writing the larger page through some hotspots.
  - If that happened, later `/api/ping` requests could not be accepted.
- Treats unknown or malformed requests as disposable.
  - Non-GET requests are closed immediately.
  - Unknown paths are closed immediately.
  - Empty/stalled clients are closed after a short timeout.
- Uses raw `WiFiServer` / `WiFiClient`.
- Every handled connection is force-closed with `client.stop()`.

## Edit credentials

Edit `Secrets.h` before uploading:

```cpp
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
```

## Test order

Open only this first:

```text
http://PRINTED_IP/api/ping
```

Expected browser result:

```text
pong
```

Expected Serial Monitor:

```text
HTTP client connected
HTTP request line: GET /api/ping HTTP/1.1
HTTP route: /api/ping
handlePing ms: ...
HTTP client closed ms: ...
```

Then test:

```text
http://PRINTED_IP/api/status
http://PRINTED_IP/api/led?value=1
http://PRINTED_IP/api/led?value=0
```

Root `/` now returns a small text message instead of the dashboard page.
