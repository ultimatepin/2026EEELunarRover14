# Metro M0 + WINC1500 WiFiWebServer Base

Arduino IDE example project for:

- Adafruit Metro M0
- Adafruit WINC1500 WiFi shield
- khoih-prog/WiFiWebServer

## Setup

1. Open `MetroM0_WINC1500_WebBase.ino` in Arduino IDE.
2. Install libraries:
   - `WiFiWebServer`
   - `WiFi101_Generic`
   - any dependencies prompted by Library Manager
3. Edit `Secrets.h`.
4. Select your Metro M0 board.
5. Upload.
6. Open Serial Monitor at `115200`.
7. Visit the printed IP address in your browser.

## Routes

- `/` basic web page
- `/api/state` JSON board state
- `/api/led?value=1` LED on
- `/api/led?value=0` LED off
- `/health` simple text health check

## Where to add your own logic

Add future pin/sensor processing mainly in:

- `BoardState.h`
- `BoardState.cpp`

Then expose the processed values through:

- `WebRoutes.cpp`
