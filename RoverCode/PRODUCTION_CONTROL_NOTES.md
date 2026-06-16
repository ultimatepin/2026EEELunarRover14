# Production WASD Dashboard Notes

## What changed from the previous WASD build

- Removed the PWM slider.
- Added a two-speed mode: `110` and `255` only.
- Added `Z` as the speed-toggle key.
- Kept immediate stop-on-release behavior.
- Simplified the UI into three production sections:
  - drive control,
  - rock telemetry,
  - compact diagnostics.
- Kept raw JSON under a collapsible details section instead of showing several large diagnostic boxes at all times.

## Control principle

The browser keeps a small object named `pressed` that represents the currently held movement keys. On every keydown/keyup it recomputes the command:

- no movement key held -> `stop`
- `W` -> `w`
- `W+D` -> `wd`
- `Q` -> `q`
- `E` -> `e`

The dashboard only sends a motor command when that computed command changes, plus a 600 ms keepalive while moving.

`Z` is not a movement key. It only flips the speed between 110 and 255. If the rover is already moving, the current command is resent immediately with the new speed.


## Motor command reliability fix

This build sends `/motor` commands using retained `XMLHttpRequest` objects instead of temporary `Image()` requests. The temporary image method could be garbage-collected or cancelled by the browser before the request reliably reached the WINC1500 server. The dashboard now shows an `ack ... HTTP 200` message when the rover confirms a motor command.

For hardware debugging, open this directly in the browser while watching Serial Monitor:

```text
http://PRINTED_IP/motor?cmd=w&speed=255
http://PRINTED_IP/motor?cmd=stop
```

A good firmware/network path prints `[MOTOR] w spd=255` and then `[MOTOR] stop spd=255` in Serial Monitor. If those logs appear but the motors do not move, the issue is motor power/driver wiring/enable pins rather than WiFi or dashboard code.
