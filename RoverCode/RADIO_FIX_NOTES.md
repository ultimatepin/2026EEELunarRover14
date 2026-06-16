# Radio receive fix notes

This build keeps the radio receiver on Metro M0 **D0 / Serial1 RX**.

## What changed

- `Serial1.begin(RADIO_BAUD)` now uses `RADIO_BAUD` from `AppConfig.h`.
- Added explicit radio config:
  - `PIN_RADIO_RX = 0`
  - `RADIO_BAUD = 600`
  - `RADIO_ACCEPT_PLAIN_3_DIGIT_AGE = true`
  - `RADIO_RAW_SERIAL_DEBUG = true`
- The parser now accepts both:
  - `#123`
  - `123`, normalised internally to `#123`
- Added raw byte logging in Serial Monitor:
  - `[RADIO RAW] dec=... hex=... char='...'`
- Added `/radio` JSON endpoint.
- Added radio diagnostics inside `/debug` and `/sensor`.
- Dashboard now shows a compact radio status line.

## Test order

1. Upload this sketch folder in Arduino IDE.
2. Open Serial Monitor at `115200`.
3. Send radio data repeatedly as either `#123` or `123` at `600` baud.
4. Open:

```text
http://PRINTED_IP/radio
http://PRINTED_IP/debug
http://PRINTED_IP/sensor
```

## Interpreting `/radio`

- `bytes: 0` means the board is not receiving any Serial1 bytes.
- `bytes` increasing but `valid_packets: 0` means the radio is reaching D0, but packet format/baud is wrong.
- `valid_packets` increasing means radio decoding works and `age` should appear.

After debugging, set this in `AppConfig.h` to reduce Serial Monitor output:

```cpp
const bool RADIO_RAW_SERIAL_DEBUG = false;
```
