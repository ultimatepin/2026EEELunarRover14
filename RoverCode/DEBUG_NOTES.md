# Debug notes for EEELunarRover_RawWiFi_Integrated_debugged

## Main changes

1. `PIN_ULTRASONIC` moved from `1` to `A1`.
   - On Metro M0, D0 is Serial1 RX and D1 is Serial1 TX.
   - The original integrated code enabled `Serial1.begin(600)` and also used D1 as the ultrasonic input.
   - Rewire the ultrasonic detector output to A1, or change `PIN_ULTRASONIC` in `AppConfig.h` to a different safe input.

2. BMM150 setup now calls `setMeasurementXYZ()`.
   - The DFRobot example initializes operation mode, preset, rate, and XYZ measurement.
   - The earlier integrated code skipped the XYZ measurement call.

3. `/dashboard` is now a small diagnostic dashboard.
   - The previous integrated dashboard was very large and could reintroduce the same hotspot/WINC1500 freezing behavior.
   - This version prioritizes testing motor commands and sensor JSON reliably.

4. Added `/debug`.
   - Shows simulated mode, BMM150 ready state, active pin map, and current motor state.

5. Added startup pin audit logs.
   - Serial Monitor now prints the active pin map and warns about obvious conflicts.

## Test order

1. Upload.
2. Open Serial Monitor at 115200.
3. Confirm the pin audit and BMM150 status.
4. Open:
   - `http://PRINTED_IP/api/ping`
   - `http://PRINTED_IP/debug`
   - `http://PRINTED_IP/sensor`
   - `http://PRINTED_IP/dashboard`
5. Test motors one command at a time:
   - `http://PRINTED_IP/motor?cmd=w&speed=120`
   - `http://PRINTED_IP/motor?cmd=stop`

## Important wiring notes

- D0/D1 are reserved for Serial1.
- D5, D7, and D10 are reserved by the WINC1500 shield.
- D4 is not interrupt-capable on Metro M0.
- The Metro M0 logic level is 3.3V. Avoid feeding 5V logic into sensor input pins.
