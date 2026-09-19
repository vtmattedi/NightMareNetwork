# DS18B20 Resource device

This example exposes a DS18B20 reading as a read-only `NetValue<float>` named `temperature`. It also exposes `connected` and `sensorAddress` Values, a `rescan` Action, and a `sensorLost` Event. Registration handles discovery, state publication, reconnect and a 60-second temperature heartbeat.

The sensor driver is a pollable state machine. It starts a 1-Wire conversion, returns to Runtime, then reads the result after the conversion interval. It creates no sensor task and sends no manual sensor JSON. The DS18B20 data line needs an external 4.7 kΩ pull-up to 3.3 V; many breakout boards already include one.

1. Copy `include/creds.example.h` to `include/creds.h` and set WiFi and local MQTT values.
2. Set `DEVICE_ID` and the board/pin selection for your hardware.
3. Run `pio run -e esp32c3-supermini` or `pio run -e esp32doit-devkit-v1`.
4. Upload and monitor with `pio run -e esp32c3-supermini -t upload -t monitor`.

The build uses the repository checkout through `symlink://../..`. The temperature Value has no retained state until the first valid reading. Consumers can use the `connected` Value to distinguish current readings from the last known temperature after a sensor loss.
