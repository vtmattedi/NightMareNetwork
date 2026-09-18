---
title: MQTTP — request/response over MQTT
description: How a client sends one command to one device and gets its reply back, including chunking, timeouts and the ids that keep replies from crossing.
section: protocols
order: 20
---

# MQTTP

MQTT has no request/response. MQTTP is the convention NightMare devices use to
fake one: a client picks an id, publishes a command on an `in` topic carrying
that id, and the device answers on the matching `out` topic. Every built-in
console command becomes a callable API this way, which is how the backend reads
a device's details and how the Dashboard's device page asks a device about
itself.

## Topics

```
request  →  <Device>/console/controlled/<id>/in     the command text
reply    ←  <Device>/console/controlled/<id>/out
```

`<id>` is anything unique per request — the backend uses 16 hex characters,
the Dashboard 8. A late reply to an abandoned request carries an id nobody is
waiting for and is ignored, which is the whole reason it exists.

Same command, no id, on `<Device>/console/in` works too and answers on
`console/out` — fine at a terminal, useless for a program, because two
programs' replies are indistinguishable.

## Reply chunking

A reply of 512 bytes or less arrives whole. Anything longer is split by the
device into 512-byte chunks, each prefixed:

```
;;<n>/<total>;;<data>
```

`n` counts from 1. The data may itself contain `;;`, so a receiver splits the
header off and treats everything after the second `;;` as payload, never
splitting further. The reply is complete when `n >= total`.

Empty payloads are dropped by `MQTT_Send()`, so a command that returns nothing
is indistinguishable from a timeout.

## Case

The device lower-cases the incoming topic to match it, but builds its reply
topic from its own `DEVICE_NAME` casing. A client that addressed `adler` gets
its reply on `Adler/console/controlled/<id>/out`. Match the reply topic
case-insensitively.

## Limits, as the consumers set them

| consumer | timeout | reply cap | why |
| --- | --- | --- | --- |
| backend | 20 s | 256 KB | it runs unattended and devices are slow |
| Dashboard | 8 s | 3 KB, truncated with a marker | someone is standing in front of it; a partial answer beats an error |

A command that takes longer than 20 s to answer — a blocking sensor probe, an
IR sequence walking twelve steps at 200 ms — reads as a failure at the backend
even when it worked. Long-running commands should answer immediately and do
the work on a task, or use the async console (`COMPILE_ASYNC_COMMANDS`).

## The async variant

The backend also implements `<Device>/console/asynccontrolled/<id>/in|out`,
where the device streams chunks and terminates with `;;:;;finished;;`, or
fails with `;;error;;<message>`. **No device implements this today**; the
library only matches `/console/controlled/`. It is documented so nobody
rediscovers it.

## From a shell

```sh
ID=$(openssl rand -hex 8)
mosquitto_sub -t "Adler/console/controlled/$ID/out" -C 1 &
mosquitto_pub -t "Adler/console/controlled/$ID/in" -m 'HARDWAREINFO'
```

```json
{"ChipModel":"ESP32-C3","ChipCores":1,"ChipRevision":4,"FlashSizeMB":4,
 "HeapSize":327680,"PsramSize":0,"MACAddress":"A0:B1:C2:D3:E4:F5"}
```

## Reply conventions

Commands that report state answer with JSON; commands that change something
answer with the new state and, where the caller needs to know it took, a
`saved`/`result` field. The adoption flow depends on one of these exactly:
`CONFIG SET` replies `{"<key>":"<value>", "saved":true}` and the backend will
not rename a device that answers anything else.
