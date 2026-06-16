# Standalone power / sensor debug notes

This build keeps the ultrasonic detector on D1, as requested.

## What this build changes

- Reduces the USB Serial wait at boot from 5000 ms to 1200 ms.
- Keeps the raw strict WiFi server behavior.
- Retries BMM150 initialization every 2 seconds if the sensor is not ready at boot.
- Adds `/i2cscan` to show which I2C devices are visible from the Metro M0.
- Adds `bmm150_init_attempts` and `uptime_ms` to `/debug`.
- Sends String responses in 64-byte chunks, like the dashboard HTML route, to reduce hotspot/WINC1500 stalls.
- Increases dashboard XHR timeout from 1500 ms to 3000 ms.
- Suppresses the large once-per-second rover status dump when USB Serial is not attached.

## Important diagnosis

If `/debug` says:

```json
"bmm150_ready": false
```

then the magnetometer did not initialize. This is usually hardware/power/wiring, not dashboard code.

Open:

```text
http://PRINTED_IP/i2cscan
```

Expected outcomes:

- `"devices":[]` means the Metro M0 cannot see anything on I2C. Check sensor power, GND, SDA/SCL, pullups, and whether the sensor is powered after USB is unplugged.
- A device address appears but `bmm150_ready:false` means I2C electrical connection exists but the BMM150 library did not accept the chip/init mode yet. Leave it powered for a few seconds; this build retries automatically.
- `bmm150_ready:true` means the BMM150 is usable.

## Standalone power checklist

When the computer is unplugged, the rover still needs:

1. Stable Metro M0 power.
2. Stable WINC1500 shield power.
3. Stable sensor power.
4. Common GND between Metro M0, sensors, and motor driver.
5. Motor power separated enough that motor current spikes do not brown out the Metro/sensors.

