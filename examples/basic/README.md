# Basic Resource device

A small ESP32 participant exposing writable brightness, read-only uptime and firmware version, RGB color state, two Actions and a transient button Event. The `setColor` Action shows a structured argument codec and ordered schema fields. The same registered Actions can be invoked through MQTT, `<device>/console/in`, or the serial Console.

1. Copy `include/creds.example.h` to `include/creds.h` and set WiFi and local MQTT values.
2. Set `DEVICE_ID` in `src/main.cpp` to a stable, unique device ID.
3. From this directory, run `pio run -e esp32c3-supermini` or `pio run -e esp32doit-devkit-v1`.
4. Upload and monitor with `pio run -e esp32c3-supermini -t upload -t monitor`.

The build uses the repository's current library through `symlink://../..`. The example calls `runtime.tick()` in `loop()`. WiFi and Console are pollable, and MQTT callbacks only enqueue messages; resource handlers run from the Runtime loop.

On connection the schema and initialized Values are published under `nm/default/<device>/r/<id>/...`. Try `toggle` or `setColor [255,120,0]` on the serial Console. An Action `ACK` sent through MQTT includes a request ID and produces a status reply on `nm/default/<caller>/reply`.
