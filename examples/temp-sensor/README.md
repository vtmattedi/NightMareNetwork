# temp-sensor

One DS18B20, done the way the protocol pages describe. What a sensor device
looks like when the backend and the Dashboard can both read it without being
told anything.

```sh
cp include/creds.example.h include/creds.h
pio run -e esp32c3-supermini -t upload -t monitor
```

What it does:

- Samples the sensor on its own FreeRTOS task, sleeping through the 750 ms
  conversion, so `loop()` is never blocked. A missing sensor is retried every
  5 s; a lost one is written off after three failed reads and searched for
  again.
- Publishes `<Device>/sensors` as one JSON object -- `{"temperature": 23.44}`,
  or `{"temperature": null}` while there is no reading -- on a change of
  0.25 C or more and every 60 s regardless.
- Answers the bare `sensors` command with the declaration the backend reads on
  discovery: `id`, `label`, `unit`, `type`, `disable`, `critical`, plus the
  hardware details the Dashboard shows.
- Publishes `<Device>/info` on every MQTT connect.
- `DS18 READ` and `DS18 STATUS` return the last sample; neither touches the bus.

The board pins come from `include/board.h`, an append-only revision registry
selected with `-D BOARD_C3_V1` or `-D BOARD_ESP32_V1` in `platformio.ini`.
