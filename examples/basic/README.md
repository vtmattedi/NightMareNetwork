# basic

The smallest NightMare device: WiFi, MQTT, the console, telemetry, and two
commands of its own. What you get from the library before writing a sensor.

```sh
cp include/creds.example.h include/creds.h     # fill in WiFi and broker
pio run -e esp32c3-supermini -t upload -t monitor
```

On the broker, within seconds of joining WiFi:

```
Esp32-nm-a1b2c3/status        online        (retained)
Esp32-nm-a1b2c3/console/out   Booted
Esp32-nm-a1b2c3/telemetry     {"System":{"Uptime":4,...}}
```

Talk to it:

```sh
mosquitto_pub -t 'Esp32-nm-a1b2c3/console/in' -m 'PING'      # -> PONG
mosquitto_pub -t 'Esp32-nm-a1b2c3/console/in' -m 'HELLO'     # -> Hello from Esp32-nm-a1b2c3
mosquitto_pub -t 'Esp32-nm-a1b2c3/console/in' -m 'ECHO a b'  # -> a b
```

The same lines work typed into the serial monitor. `include/Modules.config.h`
is the library's example config unchanged; trim the `COMPILE_*` switches once
you know what the device needs.
